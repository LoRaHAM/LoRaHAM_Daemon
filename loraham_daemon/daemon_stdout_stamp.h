#ifndef LORAHAM_DAEMON_STDOUT_STAMP_H
#define LORAHAM_DAEMON_STDOUT_STAMP_H

/* --- UTC timestamps on every log line -------------------------------------- */
/*
 * The daemon's log had no time in it at all, so a line could only be placed by
 * whatever wrote it next. Every line now starts with
 * `2026-09-16T04:22:24.420Z `, the same format the RF log already uses, so the
 * two can be read side by side.
 *
 * Why a stream wrapper and not a stamped printf: a single log line is built
 * from SEVERAL printf calls in this daemon -- the CONFIG apply path prints one
 * coloured fragment per key and ends with a newline much later. Stamping per
 * call would put a timestamp in the middle of a line and produce garbage. The
 * wrapper instead accumulates until it sees a newline and stamps the line.
 *
 * The buffer is per thread, because the TX worker and the lgpio alert callback
 * write concurrently with the main loop: a shared buffer would interleave two
 * half-lines into one. Emission takes a mutex, so a completed line reaches the
 * real stdout whole -- which the daemon did not previously guarantee either.
 *
 * Both stdout AND stderr are wrapped. The daemon writes its lock, GPIO and SPI
 * diagnostics to stderr, and an operator who redirects both into one file
 * would otherwise get a log where half the lines carry a time and half do not.
 * Each stream keeps its own partial-line buffer per thread, so a half-written
 * stdout line is never completed by a stderr write; one mutex covers both,
 * because they usually share a destination.
 *
 * Install it once, as early as possible, and after any redirection (background
 * mode re-opens the fds). Failure is not fatal: if a stream cannot be wrapped
 * the daemon keeps it unstamped rather than losing its log.
 */
void daemon_stdout_stamp_install(void);

/* Flush whatever a thread has accumulated but not yet terminated with a
 * newline. Called at shutdown so a partial line is not lost. */
void daemon_stdout_stamp_flush_partial(void);

#endif
