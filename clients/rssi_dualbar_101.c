/******************************************************************************
 *  rssi_bar_dual - Live RSSI-Balken fuer beide Baender (433 & 868) gleichzeitig
 *
 *  Verbindet sich auf /tmp/loraconf433.sock UND /tmp/loraconf868.sock,
 *  setzt FREQ und schaltet GETRSSI=1 ein. Zeigt zwei dynamische Balken
 *  uebereinander, jeweils -160 dBm links bis 0 dBm rechts.
 *
 *  Peak-Detektor: ein hellweisser '|'-Marker zeigt den Spitzenwert der letzten
 *                 5 Sekunden. Faellt danach auf den aktuellen Pegel ab.
 *
 *  Compile:  gcc -O2 -Wall -o rssi_bar_dual rssi_dualbar_101.c
 *  Usage:    ./rssi_bar_dual [Frequenz433] [Frequenz868]
 *  Default:  ./rssi_bar_dual                  -> 433.175 / 869.525 MHz
 *  Beispiel: ./rssi_bar_dual 433.775 868.300
 *  Stop:     Strg+C  (sendet sauber GETRSSI=0 an beide Daemons)
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>


/* --- Daemon-Socket-Pfadwahl -------------------------------------------------
 * systemd-Deployments servieren die Sockets unter /run/loraham, direkte/
 * Benutzer-Starts unter /tmp (LORAHAM_SOCKET_DIR). Ein Build funktioniert in
 * beiden Welten: nimm den Pfad, unter dem der Daemon-Socket tatsaechlich
 * existiert, sonst den /tmp-Fallback. */
#include <sys/stat.h>
static const char *loraham_sockpath(const char *runp, const char *tmpp)
{
    struct stat st;
    return (stat(runp, &st) == 0 && S_ISSOCK(st.st_mode)) ? runp : tmpp;
}

#define SOCKET_PATH_433 loraham_sockpath("/run/loraham/loraconf433.sock", "/tmp/loraconf433.sock")
#define SOCKET_PATH_868 loraham_sockpath("/run/loraham/loraconf868.sock", "/tmp/loraconf868.sock")
#define DEFAULT_FREQ_433 "433.175"
#define DEFAULT_FREQ_868 "869.525"
#define RSSI_MIN_DBM     -160.0f
#define RSSI_MAX_DBM        0.0f
#define MIN_BAR_WIDTH      20
#define RSSI_NA          -999.0f   /* "noch kein Wert empfangen" */
#define PEAK_HOLD_SEC       5.0    /* Peak haelt 5s, dann faellt er ab */

/* --- Globals (fuer Signal-Handler) ----------------------------------- */
static int g_sock433 = -1;
static int g_sock868 = -1;
static volatile sig_atomic_t g_run = 1;

/* --- Signal-Handler: nur Flag setzen, keine I/O ---------------------- */
static void on_sig(int sig) {
    (void)sig;
    g_run = 0;
}

/* --- aktuelle Terminal-Breite ermitteln (Fallback 80) ---------------- */
static int term_width(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return (int)w.ws_col;
    return 80;
}

/* --- Cursor ein-/ausblenden ------------------------------------------ */
static void show_cursor(int on) {
    fputs(on ? "\033[?25h" : "\033[?25l", stdout);
    fflush(stdout);
}

/* --- ANSI-Farbe nach Pegel ------------------------------------------- */
static const char *rssi_color(float rssi) {
    if (rssi <= RSSI_NA + 1.0f) return "\033[2;37m"; /* grau, noch kein Wert  */
    if (rssi >= -70.0f)         return "\033[1;31m"; /* rot fett   stark      */
    if (rssi >= -90.0f)         return "\033[33m";   /* gelb       deutlich   */
    if (rssi >= -110.0f)        return "\033[32m";   /* gruen      schwach    */
    return                              "\033[36m";  /* cyan       Rauschen   */
}

/* --- Monotone Zeit in Sekunden (fuer Peak-Hold-Timer) ---------------- */
static double now_monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* --- Peak-Hold Update -----------------------------------------------
 * Logik:
 *   - Peak noch ungesetzt              -> setze auf neuen Wert
 *   - Neuer Wert > aktueller Peak      -> Peak hoch + Timer reset
 *   - Timer >= PEAK_HOLD_SEC abgelaufen-> Peak faellt auf neuen Wert + reset
 *   - sonst                            -> Peak unveraendert (haelt)
 * Damit haengt der Peak fuer 5s an seinem Spitzenwert und faellt dann ab.
 */
static void update_peak(float new_rssi, float *peak, double *peak_set_time) {
    double now = now_monotonic();
    if (*peak <= RSSI_NA + 1.0f) {
        *peak = new_rssi;
        *peak_set_time = now;
    } else if (new_rssi > *peak) {
        *peak = new_rssi;
        *peak_set_time = now;
    } else if ((now - *peak_set_time) >= PEAK_HOLD_SEC) {
        *peak = new_rssi;
        *peak_set_time = now;
    }
}

/* --- eine Balken-Zeile in einen Puffer rendern ----------------------- */
/* peak_rssi: Spitzenwert-Position; wird als hellweisser '|' Marker
 *            ueber dem Balken eingeblendet (nur wenn peak > rssi).
 *            RSSI_NA bedeutet: kein Peak anzeigen. */
static void format_bar_line(char *out, size_t out_sz,
                            const char *band, const char *freq,
                            float rssi, float peak_rssi, int bar_width) {
    float r = rssi;
    if (r < RSSI_MIN_DBM) r = RSSI_MIN_DBM;
    if (r > RSSI_MAX_DBM) r = RSSI_MAX_DBM;
    float pos = (r - RSSI_MIN_DBM) / (RSSI_MAX_DBM - RSSI_MIN_DBM);

    int filled = (int)(pos * (float)bar_width + 0.5f);
    if (filled < 0)         filled = 0;
    if (filled > bar_width) filled = bar_width;

    /* "Noch kein Wert"-Sonderfall: leerer Balken, "wait..." statt Zahl */
    int pending = (rssi <= RSSI_NA + 1.0f);
    if (pending) filled = 0;

    /* Peak-Position berechnen (nur wenn gesetzt und > aktueller RSSI) */
    int peak_idx = -1;
    int has_peak = (peak_rssi > RSSI_NA + 1.0f);
    if (has_peak && !pending) {
        float pp = peak_rssi;
        if (pp < RSSI_MIN_DBM) pp = RSSI_MIN_DBM;
        if (pp > RSSI_MAX_DBM) pp = RSSI_MAX_DBM;
        float ppos = (pp - RSSI_MIN_DBM) / (RSSI_MAX_DBM - RSSI_MIN_DBM);
        peak_idx = (int)(ppos * (float)bar_width + 0.5f);
        if (peak_idx < 0)            peak_idx = 0;
        if (peak_idx >= bar_width)   peak_idx = bar_width - 1;
        /* Nur Marker zeichnen wenn Peak ECHT ueber dem aktuellen Pegel liegt
         * - sonst ist er ohnehin im gefuellten Block versteckt */
        if (peak_idx < filled) peak_idx = -1;
    }

    const char *col = rssi_color(rssi);

    size_t off = 0;
    int w;

    w = snprintf(out + off, out_sz - off,
                 "[%s %s MHz] -160 [", band, freq);
    if (w > 0) off += (size_t)w;

    /* gefuellt */
    if (off < out_sz) { off += (size_t)snprintf(out + off, out_sz - off, "%s", col); }
    for (int i = 0; i < filled && off + 4 < out_sz; i++) {
        out[off++] = (char)0xE2;
        out[off++] = (char)0x96;
        out[off++] = (char)0x88;   /* U+2588 FULL BLOCK */
    }

    /* leer (gedimmt), Peak-Position bekommt einen hellweissen Marker */
    if (off < out_sz) { off += (size_t)snprintf(out + off, out_sz - off, "\033[2m"); }
    for (int i = filled; i < bar_width && off + 16 < out_sz; i++) {
        if (i == peak_idx) {
            /* Peak-Marker: hellweiss U+2502 BOX VERTICAL, danach zurueck zu dim */
            off += (size_t)snprintf(out + off, out_sz - off, "\033[0;1;97m");
            out[off++] = (char)0xE2;
            out[off++] = (char)0x94;
            out[off++] = (char)0x82;   /* U+2502 │ */
            off += (size_t)snprintf(out + off, out_sz - off, "\033[2m");
        } else {
            out[off++] = (char)0xE2;
            out[off++] = (char)0x96;
            out[off++] = (char)0x91;   /* U+2591 LIGHT SHADE ░ */
        }
    }

    if (off < out_sz) { off += (size_t)snprintf(out + off, out_sz - off, "\033[0m"); }

    /* Wert + Peak rechts */
    if (pending) {
        snprintf(out + off, out_sz - off,
                 "] 0  \033[2;37mRSSI:  wait... \033[0m");
    } else if (has_peak) {
        snprintf(out + off, out_sz - off,
                 "] 0  %sRSSI: %7.2f dBm\033[0m  \033[1;97mPk: %7.2f\033[0m",
                 col, rssi, peak_rssi);
    } else {
        snprintf(out + off, out_sz - off,
                 "] 0  %sRSSI: %7.2f dBm\033[0m", col, rssi);
    }
}

/* --- beide Balken zeichnen (in-place ueber 2 Zeilen) ----------------- */
/* Erstes Mal: einfach beide Zeilen ausgeben.
 * Folgende Aufrufe: 1 Zeile hoch, beide Zeilen ueberschreiben. */
static void render_dual(const char *freq433, const char *freq868,
                        float rssi433, float peak433,
                        float rssi868, float peak868,
                        int *first_call) {
    int tw = term_width();
    /* Reservierter Platz fuer Praefix+Suffix:
     * "[433 XXX.XXX MHz] -160 [" = ca. 24 + len(freq)
     * "] 0  RSSI: -123.45 dBm  Pk: -123.45" = ca. 35 */
    int reserved = 24 + (int)strlen(freq433) + 35;
    int bw = tw - reserved;
    if (bw < MIN_BAR_WIDTH) bw = MIN_BAR_WIDTH;

    /* Buffer pro Zeile, grosszuegig dimensioniert (Marker bringt mehr ANSI-Bytes mit) */
    char line433[1536];
    char line868[1536];
    format_bar_line(line433, sizeof(line433), "433", freq433, rssi433, peak433, bw);
    format_bar_line(line868, sizeof(line868), "868", freq868, rssi868, peak868, bw);

    if (*first_call) {
        /* Erstausgabe: zwei Zeilen mit Newline, danach Cursor steht in Zeile 3 */
        printf("%s\n", line433);
        printf("%s\n", line868);
        *first_call = 0;
    } else {
        /* Update: 2 Zeilen hoch, dann beide Zeilen neu schreiben.
         *   \033[2F   = 2 Zeilen hoch, Cursor an Anfang
         *   \033[2K   = ganze Zeile loeschen
         *   \r        = sicherheitshalber an Spalte 0 */
        fputs("\033[2F", stdout);
        fputs("\r\033[2K", stdout); printf("%s\n", line433);
        fputs("\r\033[2K", stdout); printf("%s\n", line868);
    }
    fflush(stdout);
}

/* --- Verbindung zu einem Conf-Socket aufbauen ------------------------ */
static int connect_socket(const char *path) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        int e = errno;
        close(s);
        errno = e;
        return -1;
    }
    return s;
}

/* --- FREQ + GETRSSI=1 senden ----------------------------------------- */
static int activate_stream(int sock, const char *freq) {
    char cmd[128];
    int n = snprintf(cmd, sizeof(cmd), "SET FREQ=%s\n", freq);
    if (write(sock, cmd, (size_t)n) < 0) return -1;
    usleep(50000);
    if (write(sock, "SET GETRSSI=1\n", 14) < 0) return -1;
    return 0;
}

/* --- empfangene Bytes zu Zeilen zusammenbauen, RSSI extrahieren ------ */
/* Liefert true, wenn ein neuer RSSI-Wert in *out gelegt wurde. */
static int feed_and_parse(const char *data, ssize_t len,
                          char *line_buf, size_t line_sz, size_t *line_pos,
                          float *out) {
    int got_new = 0;
    for (ssize_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n' || *line_pos + 1 >= line_sz) {
            line_buf[*line_pos] = '\0';
            if (strncmp(line_buf, "RSSI=", 5) == 0) {
                *out = (float)atof(line_buf + 5);
                got_new = 1;
            }
            /* andere Zeilen (CAD=, Echo) werden ignoriert */
            *line_pos = 0;
        } else if (c != '\r') {
            line_buf[(*line_pos)++] = c;
        }
    }
    return got_new;
}

/* --- main ------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    const char *freq433 = (argc > 1) ? argv[1] : DEFAULT_FREQ_433;
    const char *freq868 = (argc > 2) ? argv[2] : DEFAULT_FREQ_868;

    /* Signal-Handler installieren */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sig;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);

    /* Beide Sockets verbinden */
    g_sock433 = connect_socket(SOCKET_PATH_433);
    if (g_sock433 < 0) {
        fprintf(stderr, "connect %s: %s\n", SOCKET_PATH_433, strerror(errno));
        fprintf(stderr, "Laeuft loraham_daemon?\n");
        return 2;
    }
    g_sock868 = connect_socket(SOCKET_PATH_868);
    if (g_sock868 < 0) {
        fprintf(stderr, "connect %s: %s\n", SOCKET_PATH_868, strerror(errno));
        close(g_sock433);
        return 2;
    }

    /* Streams aktivieren */
    if (activate_stream(g_sock433, freq433) < 0) {
        perror("activate 433");
        close(g_sock433); close(g_sock868);
        return 3;
    }
    if (activate_stream(g_sock868, freq868) < 0) {
        perror("activate 868");
        close(g_sock433); close(g_sock868);
        return 3;
    }

    show_cursor(0);
    fprintf(stderr, "rssi_bar_dual: 433=%s MHz, 868=%s MHz - Strg+C zum Beenden\n",
            freq433, freq868);

    /* Empfangs-Loop ueber select() auf beide Sockets gleichzeitig */
    char  buf[512];
    char  line433[256]; size_t lp433 = 0;
    char  line868[256]; size_t lp868 = 0;
    float rssi433 = RSSI_NA;
    float rssi868 = RSSI_NA;
    /* Peak-State: Spitzenwert der letzten 5s pro Band */
    float peak433 = RSSI_NA;
    float peak868 = RSSI_NA;
    double peak_t433 = 0.0;
    double peak_t868 = 0.0;
    int   first   = 1;
    int   dirty   = 1;     /* erstes Frame sofort zeichnen */

    while (g_run) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(g_sock433, &rfds);
        FD_SET(g_sock868, &rfds);
        int maxfd = (g_sock433 > g_sock868 ? g_sock433 : g_sock868);

        /* 100ms Timeout: wenn nichts kommt, zeichnen wir trotzdem
         * (z.B. damit "wait..." sichtbar wird, oder Peak abfaellt) */
        struct timeval tv = { 0, 100000 };
        int rv = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rv < 0) {
            if (errno == EINTR) continue;
            perror("\nselect");
            break;
        }

        if (rv > 0) {
            if (FD_ISSET(g_sock433, &rfds)) {
                ssize_t got = read(g_sock433, buf, sizeof(buf));
                if (got <= 0) {
                    fputs("\n433-Daemon hat Verbindung geschlossen.\n", stderr);
                    break;
                }
                if (feed_and_parse(buf, got, line433, sizeof(line433),
                                   &lp433, &rssi433)) {
                    update_peak(rssi433, &peak433, &peak_t433);
                    dirty = 1;
                }
            }
            if (FD_ISSET(g_sock868, &rfds)) {
                ssize_t got = read(g_sock868, buf, sizeof(buf));
                if (got <= 0) {
                    fputs("\n868-Daemon hat Verbindung geschlossen.\n", stderr);
                    break;
                }
                if (feed_and_parse(buf, got, line868, sizeof(line868),
                                   &lp868, &rssi868)) {
                    update_peak(rssi868, &peak868, &peak_t868);
                    dirty = 1;
                }
            }
        }

        if (dirty) {
            render_dual(freq433, freq868,
                        rssi433, peak433,
                        rssi868, peak868,
                        &first);
            dirty = 0;
        }
    }

    /* Sauber beenden: Streams stoppen */
    ssize_t wr;
    wr = write(g_sock433, "SET GETRSSI=0\n", 14); (void)wr;
    wr = write(g_sock868, "SET GETRSSI=0\n", 14); (void)wr;
    show_cursor(1);
    fputs("\n", stdout);
    close(g_sock433);
    close(g_sock868);
    return 0;
}


