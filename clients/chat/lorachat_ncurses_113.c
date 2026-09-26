/******************************************************************************
 * Copyright (C) 2026  LoRaHAM / Alexander Walter
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <https://www.gnu.org/licenses/>.
 *****************************************************************************/

/*

/*
  sudo apt-get install libncurses5-dev libncursesw5-dev

  gcc lorachat_ncurses_113.c -o lorachat -lncurses -lpthread

 */

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <ctype.h>
#include <time.h>


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

#define SOCKET_PATH loraham_sockpath("/run/loraham/lora433.sock", "/tmp/lora433.sock")
#define CONFIG_SOCKET_PATH loraham_sockpath("/run/loraham/loraconf433.sock", "/tmp/loraconf433.sock")

/* How long to wait for a transmission to complete (see wait_for_tx_complete). */
#define TX_WAIT_POLL_US      100000   /* 100 ms between counter reads          */
#define TX_WAIT_MAX_POLLS    300      /* backstop: 30 s, then retune regardless */
#define TX_WAIT_FALLBACK_US  3000000  /* 3 s, only if the daemon cannot be asked */
#define CONFIG_FILE "lorachat.conf"
#define CHAT_LOG "lorachat.log"
#define MAX_MSG_LEN 256
#define MAX_CALLS 30
#define CHAT_BACKLOG 50

typedef struct {
    char sender[20];
    char text[MAX_MSG_LEN];
    char time_str[9]; // "HH:MM"
    int color_pair;
} ChatLine;

char TX_FREQ[10] = "433.775";
char RX_FREQ[10] = "433.900";

// --- Prototypen ---
void update_footer();
void update_call_list();
void setup_windows();
void to_uppercase(char *str);
void open_config_menu();
void add_call(const char *call);
void add_to_history(const char* sender, const char* text, int pair);
void redraw_chat();
void send_lora_config(const char* freq);
void load_config();
void save_config();
void save_chat_history();
void load_chat_history();

ChatLine chat_history[CHAT_BACKLOG];
int history_count = 0;

char CALL_SIGN[20] = "DL0XXX-10";
char CALL_SIGN_STOP[] = ">";
char APRS_PATH[30] = "APRS,WIDE1-1";
char SEPERATOR[] = "::";
char destination_callsign[10] = "ALL      ";

char heard_calls[MAX_CALLS][20] = {"Calls:", "", "ALL", "APRSPH", "DC2WA-15"};
int call_count = 5;

int sock_fd;
WINDOW *title_win, *call_win, *chat_win, *input_win, *footer_win;

// --- Chat-Sicherung mit Zeitstempel ---

void save_chat_history() {
    FILE *f = fopen(CHAT_LOG, "w");
    if (!f) return;
    for (int i = 0; i < history_count; i++) {
        char clean_text[MAX_MSG_LEN];
        strncpy(clean_text, chat_history[i].text, MAX_MSG_LEN-1);
        clean_text[MAX_MSG_LEN-1] = '\0';

        for (int j = 0; clean_text[j] != '\0'; j++) {
            if ((unsigned char)clean_text[j] < 32 || (unsigned char)clean_text[j] == 127) {
                clean_text[j] = '.';
            }
        }
        // Format: Farbe|Zeit|Absender|Text
        fprintf(f, "%d|%s|%s|%s\n", 
                chat_history[i].color_pair, 
                chat_history[i].time_str, 
                chat_history[i].sender, 
                clean_text);
    }
    fclose(f);
}

void load_chat_history() {
    FILE *f = fopen(CHAT_LOG, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f) && history_count < CHAT_BACKLOG) {
        char *pair_str = strtok(line, "|");
        char *time_str = strtok(NULL, "|");
        char *sender_str = strtok(NULL, "|");
        char *text_str = strtok(NULL, "\n");

        if (pair_str && time_str && sender_str && text_str) {
            chat_history[history_count].color_pair = atoi(pair_str);
            strncpy(chat_history[history_count].time_str, time_str, 8);
            strncpy(chat_history[history_count].sender, sender_str, 19);
            strncpy(chat_history[history_count].text, text_str, MAX_MSG_LEN - 1);
            history_count++;
        }
    }
    fclose(f);
}

// --- Hilfsfunktionen ---

void to_uppercase(char *str) {
    while (*str) { *str = toupper((unsigned char)*str); str++; }
}

void load_config() {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *val = strchr(line, '=');
        if (!val) continue;
        *val = '\0'; val++;
        val[strcspn(val, "\r\n")] = 0;
        if (strcmp(line, "CALL") == 0) strncpy(CALL_SIGN, val, 19);
        else if (strcmp(line, "PATH") == 0) strncpy(APRS_PATH, val, 29);
        else if (strcmp(line, "DEST") == 0) snprintf(destination_callsign, 10, "%-9s", val);
        else if (strcmp(line, "TX") == 0) strncpy(TX_FREQ, val, 9);
        else if (strcmp(line, "RX") == 0) strncpy(RX_FREQ, val, 9);
    }
    fclose(f);
}

void save_config() {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f) return;
    fprintf(f, "CALL=%s\n", CALL_SIGN);
    fprintf(f, "PATH=%s\n", APRS_PATH);
    char temp_dest[10];
    strncpy(temp_dest, destination_callsign, 9);
    temp_dest[9] = '\0';
    char *end = temp_dest + strlen(temp_dest) - 1;
    while(end > temp_dest && isspace((unsigned char)*end)) end--;
    *(end+1) = '\0';
    fprintf(f, "DEST=%s\n", temp_dest);
    fprintf(f, "TX=%s\n", TX_FREQ);
    fprintf(f, "RX=%s\n", RX_FREQ);
    fclose(f);
}

void send_lora_config(const char* freq) {
    int cfg_fd;
    struct sockaddr_un addr;
    char config_cmd[256];
    cfg_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cfg_fd < 0) return;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONFIG_SOCKET_PATH, sizeof(addr.sun_path)-1);
    if (connect(cfg_fd, (struct sockaddr*)&addr, sizeof(addr)) != -1) {
        snprintf(config_cmd, sizeof(config_cmd),
                 "SET FREQ=%s SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=AUTO POWER=17",
                 freq);
        send(cfg_fd, config_cmd, strlen(config_cmd), 0);
    }
    close(cfg_fd);
}

/*
 * Ask the daemon one question and read its answer. `send_lora_config` above
 * deliberately stays write-only (it is called from the receive thread too);
 * this is the read path used to find out when a transmission has finished.
 */
static int config_query(const char *cmd, char *out, size_t out_len) {
    int fd;
    struct sockaddr_un addr;
    ssize_t n;
    if (!out || out_len == 0) return -1;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONFIG_SOCKET_PATH, sizeof(addr.sun_path)-1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) { close(fd); return -1; }
    if (send(fd, cmd, strlen(cmd), 0) < 0) { close(fd); return -1; }
    n = recv(fd, out, out_len - 1, 0);
    close(fd);
    if (n <= 0) return -1;
    out[n] = '\0';
    return 0;
}

/*
 * How many transmissions the daemon has FINISHED WITH, by any route: TXOK+TXERR
 * plus TXBUSY. TXBUSY counts a frame the daemon refused because the radio was
 * already busy -- it never becomes TXOK or TXERR, so leaving it out would mean a
 * refused frame moves no counter at all and we would sit out the whole backstop
 * before retuning. The frame is gone in that case too, so retuning at once is
 * both quicker and the honest answer.
 *
 * Returns -1 when the daemon cannot be asked. GET STATS is a counter read: it
 * never touches the radio, so polling it is free.
 */
static long tx_completed_count(void) {
    char buf[512];
    const char *ok, *err, *busy;
    if (config_query("GET STATS\n", buf, sizeof(buf)) != 0) return -1;
    ok   = strstr(buf, "TXOK=");
    err  = strstr(buf, "TXERR=");
    busy = strstr(buf, "TXBUSY=");
    if (!ok || !err || !busy) return -1;
    return strtol(ok + 5, NULL, 10)
         + strtol(err + 6, NULL, 10)
         + strtol(busy + 7, NULL, 10);
}

/*
 * Block until the frame we just handed over has actually left the radio.
 *
 * A fixed sleep cannot do this. Time-on-air alone is ~2.6 s for a short frame at
 * SF12/BW125, and in MANAGED mode the daemon additionally waits for a clear
 * channel (CAD/LBT) and may queue behind another stack — a delay with no upper
 * bound that any constant would eventually lose to. So wait on the daemon's own
 * completion counter instead, and treat the timeout purely as a backstop.
 */
static int tx_busy_state(void) {
    char buf[512];
    const char *b;
    /* NOSCAN: the passive form. The plain GET CHANNEL runs a CAD scan, which
       would fight the very transmission we are waiting on. */
    if (config_query("GET CHANNEL NOSCAN\n", buf, sizeof(buf)) != 0) return -1;
    b = strstr(buf, "BUSY=");
    if (!b) return -1;
    return strtol(b + 5, NULL, 10) != 0 ? 1 : 0;
}

static void wait_for_tx_complete(long before) {
    int i;
    if (before < 0) {          /* stats unavailable: fall back to a generous wait */
        usleep(TX_WAIT_FALLBACK_US);
        return;
    }
    for (i = 0; i < TX_WAIT_MAX_POLLS; i++) {
        long now;
        usleep(TX_WAIT_POLL_US);
        now = tx_completed_count();
        if (now < 0 || now <= before) continue;
        /* The counter belongs to the BAND daemon, not to us: another client of the
           same band moves it too. The BUSY check below catches the case where such
           a frame finishes while OURS is in the air.

           It does not catch the other one -- a foreign frame finishing while ours
           is still queued behind the channel-free wait, because tx_busy is only
           set at acquire, after that wait, so BUSY reads 0 for a frame that has
           not started. What makes this safe is not the check but the product
           rule: one app stack per band daemon (chat is refused while another
           holds 433), so in a supported deployment this counter IS ours alone. A
           second client started by hand on the same band sits outside that rule.

           Closing it properly belongs in the daemon, which is the only party that
           knows when its TX worker released the radio -- a daemon behaviour
           change, not a client patch. So: rule is the proof, check is the belt. */
        if (tx_busy_state() != 1) return;
    }
}

void add_to_history(const char* sender, const char* text, int pair) {
    ChatLine new_line;
    strncpy(new_line.sender, sender, 19);
    strncpy(new_line.text, text, MAX_MSG_LEN - 1);
    new_line.color_pair = pair;

    // Aktuelle Uhrzeit holen
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(new_line.time_str, sizeof(new_line.time_str), "%H:%M", t);

    if (history_count < CHAT_BACKLOG) {
        chat_history[history_count++] = new_line;
    } else {
        for (int i = 1; i < CHAT_BACKLOG; i++) chat_history[i-1] = chat_history[i];
        chat_history[CHAT_BACKLOG-1] = new_line;
    }
    save_chat_history();
}

void redraw_chat() {
    wclear(chat_win);
    for (int i = 0; i < history_count; i++) {
        // 1. Uhrzeit (Dunkelblau / Pair 7)
        wattron(chat_win, COLOR_PAIR(7));
        wprintw(chat_win, "%s ", chat_history[i].time_str);
        wattroff(chat_win, COLOR_PAIR(7));

        // 2. Rufzeichen (Fett)
        wattron(chat_win, COLOR_PAIR(chat_history[i].color_pair) | A_BOLD);
        wprintw(chat_win, "%-10s: ", chat_history[i].sender);
        wattroff(chat_win, A_BOLD);

        // 3. Nachrichtentext
        if (chat_history[i].color_pair == 3) {
            wattroff(chat_win, COLOR_PAIR(3));
            wattron(chat_win, COLOR_PAIR(6));
            wprintw(chat_win, "%s\n", chat_history[i].text);
            wattroff(chat_win, COLOR_PAIR(6));
        } else {
            wprintw(chat_win, "%s\n", chat_history[i].text);
            wattroff(chat_win, COLOR_PAIR(2));
        }
    }
    wnoutrefresh(chat_win);
}

void update_call_list() {
    wclear(call_win);
    wbkgd(call_win, COLOR_PAIR(4));
    for (int i = 0; i < call_count; i++) {
        if (i == 0) wattron(call_win, A_BOLD);
        mvwprintw(call_win, i, 1, "%-10s", heard_calls[i]);
        if (i == 0) wattroff(call_win, A_BOLD);
    }
    wnoutrefresh(call_win);
}

void update_footer() {
    int h, w; getmaxyx(stdscr, h, w);
    wclear(footer_win);
    wbkgd(footer_win, COLOR_PAIR(1));
    wattron(footer_win, COLOR_PAIR(1));
    wprintw(footer_win, " Ctrl-K: Konf | TX: %s | RX: %s | Ziel: ", TX_FREQ, RX_FREQ);
    wattron(footer_win, COLOR_PAIR(5) | A_BOLD);
    waddstr(footer_win, destination_callsign);
    wattroff(footer_win, COLOR_PAIR(5) | A_BOLD);
    wnoutrefresh(footer_win);
}

void add_call(const char *call) {
    if (!call || strlen(call) < 3) return;
    char up_call[20];
    strncpy(up_call, call, 19);
    up_call[19] = '\0';
    to_uppercase(up_call);
    for (int i = 0; i < call_count; i++) {
        if (strcmp(heard_calls[i], up_call) == 0) return;
    }
    if (call_count < MAX_CALLS) {
        strncpy(heard_calls[call_count++], up_call, 19);
        update_call_list();
        doupdate();
    }
}

void setup_windows() {
    int h, w; getmaxyx(stdscr, h, w);
    if(title_win) delwin(title_win);
    if(call_win) delwin(call_win);
    if(chat_win) delwin(chat_win);
    if(input_win) delwin(input_win);
    if(footer_win) delwin(footer_win);
    clear(); refresh();
    title_win = newwin(1, w, 0, 0);
    call_win = newwin(h - 3, 11, 1, 0);
    chat_win = newwin(h - 3, w - 11, 1, 11);
    input_win = newwin(1, w - 2, h - 2, 1);
    footer_win = newwin(1, w, h - 1, 0);
    wbkgd(title_win, COLOR_PAIR(1));
    wbkgd(chat_win, COLOR_PAIR(4));
    scrollok(chat_win, TRUE);
    mvwprintw(title_win, 0, (w - 20) / 2, "LoRaHAM_Pi Chat (C)");
    wnoutrefresh(title_win);
    redraw_chat();
}

void open_config_menu() {
    int h, w; getmaxyx(stdscr, h, w);
    int win_h = 14, win_w = 50;
    WINDOW *cfg_win = newwin(win_h, win_w, (h - win_h) / 2, (w - win_w) / 2);
    keypad(cfg_win, TRUE);
    wbkgd(cfg_win, COLOR_PAIR(1));
    while (1) {
        wclear(cfg_win); box(cfg_win, 0, 0);
        mvwprintw(cfg_win, 1, (win_w - 15) / 2, " KONFIGURATION ");
        mvwprintw(cfg_win, 3, 2, "1. Eigenes Call: %s", CALL_SIGN);
        mvwprintw(cfg_win, 4, 2, "2. APRS-Pfad   : %s", APRS_PATH);
        mvwprintw(cfg_win, 5, 2, "3. Ziel-Call   : %s", destination_callsign);
        mvwprintw(cfg_win, 6, 2, "4. TX Frequenz : %s", TX_FREQ);
        mvwprintw(cfg_win, 7, 2, "5. RX Frequenz : %s", RX_FREQ);
        mvwprintw(cfg_win, 10, 2, "Wähle 1-5 oder ESC zum Beenden");
        wrefresh(cfg_win);
        int ch = wgetch(cfg_win);
        if (ch == 27) break;
        echo(); curs_set(1);
        if (ch == '1') {
            mvwgetnstr(cfg_win, 8, 14, CALL_SIGN, 15); to_uppercase(CALL_SIGN);
        } else if (ch == '2') {
            mvwgetnstr(cfg_win, 8, 14, APRS_PATH, 25);
        } else if (ch == '3') {
            char b[10]; mvwgetnstr(cfg_win, 8, 14, b, 9); to_uppercase(b);
            snprintf(destination_callsign, 10, "%-9s", b);
        } else if (ch == '4') {
            mvwgetnstr(cfg_win, 8, 14, TX_FREQ, 9);
        } else if (ch == '5') {
            mvwgetnstr(cfg_win, 8, 14, RX_FREQ, 9);
            send_lora_config(RX_FREQ);
        }
        noecho(); curs_set(0);
    }
    save_config();
    delwin(cfg_win);
    setup_windows();
    update_call_list();
    update_footer();
    doupdate();
}

void send_aprs(const char *text) {
    long tx_before;
    int split_freq;
    if (strlen(text) == 0) return;
    /* With TX == RX there is nothing to retune, so there is nothing to wait for:
       skip the whole dance and keep that configuration as fast as it ever was. */
    split_freq = (strcmp(TX_FREQ, RX_FREQ) != 0);
    /* Snapshot BEFORE retuning: we wait for this number to move, so a stale
       read here would be indistinguishable from a completed transmit. */
    tx_before = split_freq ? tx_completed_count() : -2;
    send_lora_config(TX_FREQ);
    usleep(50000);
    unsigned char packet[1024];
    int len = 0;
    packet[len++] = 0x3c; packet[len++] = 0xff; packet[len++] = 0x01;
    len += sprintf((char*)&packet[len], "%s%s%s%s%s:%s",
                   CALL_SIGN, CALL_SIGN_STOP, APRS_PATH, SEPERATOR, destination_callsign, text);
    send(sock_fd, packet, len, 0);
    if (split_freq) {
        /* The frame is only QUEUED at this point. Retuning now would move the
           radio out from under a transmission still in progress -- that is the
           bug this waits out. */
        wait_for_tx_complete(tx_before);
        send_lora_config(RX_FREQ);
    }
    add_to_history("Ich", text, 2);
    redraw_chat();
}

void *receive_thread(void *arg) {
    unsigned char buf[1024];
    send_lora_config(RX_FREQ);
    while (1) {
        int n = recv(sock_fd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            char sender[20] = "UNK";
            char *msg_content = "---";
            char *tag_open = strchr((char*)buf, '<');
            char *tag_close = strchr((char*)buf, '>');
            if (tag_open && tag_close && (tag_close > tag_open)) {
                char *call_start = tag_open + 3;
                int s_len = tag_close - call_start;
                if (s_len > 0 && s_len < 20) {
                    strncpy(sender, call_start, s_len);
                    sender[s_len] = '\0';
                    add_call(sender);
                }
            }
            char *first_colon = strchr((char*)buf, ':');
            if (first_colon) {
                if (*(first_colon + 1) == ':') msg_content = first_colon + 2;
                else msg_content = first_colon + 1;
            }
            for (int i = 0; msg_content[i] != '\0'; i++) {
                if ((unsigned char)msg_content[i] < 32 && msg_content[i] != '\n') {
                    msg_content[i] = ' ';
                }
            }
            add_to_history(sender, msg_content, 3);
            redraw_chat();
            doupdate();
        }
    }
    return NULL;
}

int main() {
    load_config();
    load_chat_history();

    struct sockaddr_un addr;
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        printf("Socket Fehler!\n"); return 1;
    }
    initscr(); start_color(); cbreak(); noecho(); curs_set(0);
    init_pair(1, COLOR_WHITE,  COLOR_BLUE);
    init_pair(2, COLOR_CYAN,   COLOR_BLACK);
    init_pair(3, COLOR_GREEN,  COLOR_BLACK);
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
    init_pair(5, COLOR_RED,    COLOR_BLUE);
    init_pair(6, 11,           COLOR_BLACK);
    init_pair(7, COLOR_BLUE,   COLOR_BLACK); // FARBE FÜR DIE UHRZEIT
    
    setup_windows(); update_footer(); update_call_list(); doupdate();
    pthread_t tid;
    pthread_create(&tid, NULL, receive_thread, NULL);
    char input_buf[256];
    int pos = 0;
    while (1) {
        int ch = wgetch(input_win);
        if (ch == KEY_RESIZE) {
            setup_windows(); update_footer(); update_call_list(); doupdate();
            mvwaddstr(input_win, 0, 0, "> "); waddnstr(input_win, input_buf, pos);
        } else if (ch == 11 || ch == 18) {
            open_config_menu();
        } else if (ch == 10 || ch == 13) {
            input_buf[pos] = '\0'; send_aprs(input_buf);
            pos = 0; wclear(input_win); mvwaddstr(input_win, 0, 0, "> ");
        } else if (ch >= 32 && ch <= 126 && pos < 255) {
            input_buf[pos++] = ch; waddch(input_win, ch);
        } else if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && pos > 0) {
            pos--; mvwaddch(input_win, 0, pos + 2, ' '); wmove(input_win, 0, pos + 2);
        }
        wrefresh(input_win);
    }
    endwin(); return 0;
}
