/*
 * UTC timestamps on the daemon's log lines.
 *
 * The thing that makes this non-trivial, and the reason it is a stream wrapper
 * rather than a stamped printf: a single log line in this daemon is built from
 * SEVERAL printf calls. The CONFIG apply path prints one coloured fragment per
 * key and only later a newline; "[GPIO] pin locks held:" is followed by one
 * fprintf per pin. Stamping per call would put a timestamp in the middle of a
 * line.
 *
 * Each case runs the writing in a forked child with stdout pointed at a file,
 * because the wrapper replaces `stdout` for the whole process and the test's
 * own output must stay readable.
 */

#include "../daemon_stdout_stamp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

static int g_ok = 0;
static int g_fail = 0;

static void expect(const char *name, bool cond, const char *detail)
{
    if (cond) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: %s\n", name, detail);
    }
}

/* Run `body` in a child whose stdout is `path`, then return the file's text. */
static std::string capture(const char *path, void (*body)(void))
{
    fflush(NULL);

    pid_t pid = fork();
    if (pid == 0) {
        if (!freopen(path, "w", stdout))
            _exit(2);
        daemon_stdout_stamp_install();
        body();
        daemon_stdout_stamp_flush_partial();
        fflush(NULL);
        _exit(0);
    }

    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid)
        return std::string();

    FILE *f = fopen(path, "r");
    if (!f)
        return std::string();

    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    fclose(f);
    unlink(path);
    return text;
}

static size_t count_lines(const std::string &s)
{
    size_t n = 0;
    for (char c : s)
        if (c == '\n')
            n++;
    return n;
}

/* A stamp is `2026-09-16T08:22:31.003Z ` -- the same shape the RF log uses, so
 * the two can be read side by side. */
static bool looks_stamped(const std::string &line)
{
    if (line.size() < 25)
        return false;
    return line[4] == '-' && line[7] == '-' && line[10] == 'T' &&
           line[13] == ':' && line[16] == ':' && line[19] == '.' &&
           line[23] == 'Z' && line[24] == ' ';
}

/* The case the wrapper exists for. */
static void body_multi_call_line(void)
{
    printf("[GPIO] pin locks held:");
    for (int pin = 1; pin <= 4; pin++)
        printf(" %d", pin);
    printf("\n");
}

static void test_a_line_built_from_many_calls_is_one_stamped_line(void)
{
    const std::string out = capture("/tmp/loraham-stamp-multi.txt",
                                   body_multi_call_line);

    expect("a line built from six printf calls is exactly one line",
           count_lines(out) == 1,
           "per-call stamping would put a timestamp inside the line");
    expect("that line is stamped", looks_stamped(out), out.c_str());
    expect("and carries the whole assembled text",
           out.find("[GPIO] pin locks held: 1 2 3 4") != std::string::npos,
           out.c_str());
}

static void body_three_lines(void)
{
    printf("first\n");
    printf("second\n");
    printf("third\n");
}

static void test_every_line_is_stamped(void)
{
    const std::string out = capture("/tmp/loraham-stamp-three.txt",
                                    body_three_lines);

    expect("three writes give three lines", count_lines(out) == 3, out.c_str());

    size_t start = 0, stamped = 0;
    for (;;) {
        const size_t nl = out.find('\n', start);
        if (nl == std::string::npos)
            break;
        if (looks_stamped(out.substr(start, nl - start)))
            stamped++;
        start = nl + 1;
    }
    expect("all three are stamped", stamped == 3, out.c_str());
}

/* stderr carries the lock, GPIO and SPI diagnostics. An operator redirecting
 * both streams into one file must not get half a log with times in it. */
static void body_stderr_line(void)
{
    fprintf(stderr, "[LOCK] error:");
    fprintf(stderr, " lock directory unusable\n");
}

static void test_stderr_is_stamped_too(void)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        /* Both streams to the same file, as a redirected log would be. */
        if (!freopen("/tmp/loraham-stamp-err.txt", "w", stdout))
            _exit(2);
        if (dup2(fileno(stdout), fileno(stderr)) < 0)
            _exit(2);
        daemon_stdout_stamp_install();
        body_stderr_line();
        daemon_stdout_stamp_flush_partial();
        fflush(NULL);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);

    FILE *f = fopen("/tmp/loraham-stamp-err.txt", "r");
    std::string text;
    if (f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            text.append(buf, n);
        fclose(f);
        unlink("/tmp/loraham-stamp-err.txt");
    }

    expect("a stderr line built from two calls is one line",
           count_lines(text) == 1, text.c_str());
    expect("the stderr line is stamped", looks_stamped(text), text.c_str());
    expect("and is whole",
           text.find("[LOCK] error: lock directory unusable") != std::string::npos,
           text.c_str());
}

/*
 * The TX worker and the lgpio alert callback write concurrently with the main
 * loop. With one shared buffer their half-lines would be spliced together; the
 * buffer is per thread and per stream, so each line comes out whole.
 */
static void body_two_threads(void)
{
    std::thread a([] {
        for (int i = 0; i < 50; i++) {
            printf("AAAA");
            printf("AAAA\n");
        }
    });
    std::thread b([] {
        for (int i = 0; i < 50; i++) {
            printf("BBBB");
            printf("BBBB\n");
        }
    });
    a.join();
    b.join();
}

static void test_two_threads_do_not_splice_lines(void)
{
    const std::string out = capture("/tmp/loraham-stamp-threads.txt",
                                    body_two_threads);

    size_t start = 0, lines = 0, clean = 0;
    for (;;) {
        const size_t nl = out.find('\n', start);
        if (nl == std::string::npos)
            break;
        const std::string line = out.substr(start, nl - start);
        lines++;
        if (line.find("AAAAAAAA") != std::string::npos ||
            line.find("BBBBBBBB") != std::string::npos) {
            /* A spliced line would contain both letters. */
            if (!(line.find('A') != std::string::npos &&
                  line.find('B') != std::string::npos))
                clean++;
        }
        start = nl + 1;
    }

    expect("both threads produced all their lines", lines == 100, out.c_str());
    expect("no line mixes the two threads' output", clean == 100,
           "a shared partial-line buffer splices concurrent writers");
}

/* A line that was built but never terminated must not be lost at shutdown. */
static void body_unterminated(void)
{
    printf("half a line with no newline");
}

static void test_a_partial_line_is_flushed(void)
{
    const std::string out = capture("/tmp/loraham-stamp-partial.txt",
                                    body_unterminated);

    expect("the unterminated line was emitted", count_lines(out) == 1,
           out.c_str());
    expect("it is stamped like any other", looks_stamped(out), out.c_str());
    expect("and carries its text",
           out.find("half a line with no newline") != std::string::npos,
           out.c_str());
}

int main(void)
{
    test_a_line_built_from_many_calls_is_one_stamped_line();
    test_every_line_is_stamped();
    test_stderr_is_stamped_too();
    test_two_threads_do_not_splice_lines();
    test_a_partial_line_is_flushed();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
