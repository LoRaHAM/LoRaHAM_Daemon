/******************************************************************************
 * Copyright (C) 2026  [LoRaHAM / Alexander Walter]
 * * LICENSE: GNU General Public License v3 (GPLv3) with the following terms:
 * 1. PRIVATE/HOBBY: Free use, modification, and redistribution for non-commercial
 * purposes is permitted.
 * 2. COMMERCIAL: Commercial or business use is STRICTLY PROHIBITED unless a
 * written license is obtained from the author for a fee (Dual-Licensing).
 * [CONTACT: loraham.de Email Contact]
 * 3. CODE MAINTENANCE: Any modifications to this code must be reported to the
 * author (preferably via Pull Request on GitHub).
 * 4. REDISTRIBUTION: Binaries may only be distributed alongside the full
 * source code (Copyleft) (Copyleft).
 * * --- DISCLAIMER OF WARRANTY & LIMITATION OF LIABILITY ---
 * THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE
 * PROGRAM IS WITH THE USER.
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

#define SOCKET_PATH "/tmp/lora433.sock"
#define CONFIG_SOCKET_PATH "/tmp/loraconf433.sock"
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
    if (strlen(text) == 0) return;
    send_lora_config(TX_FREQ);
    usleep(50000);
    unsigned char packet[1024];
    int len = 0;
    packet[len++] = 0x3c; packet[len++] = 0xff; packet[len++] = 0x01;
    len += sprintf((char*)&packet[len], "%s%s%s%s%s:%s",
                   CALL_SIGN, CALL_SIGN_STOP, APRS_PATH, SEPERATOR, destination_callsign, text);
    send(sock_fd, packet, len, 0);
    usleep(100000);
    send_lora_config(RX_FREQ);
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
