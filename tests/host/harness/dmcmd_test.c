/* dmcmd() of libtr098 with 0 B .. 3 MB of output (tests/host/run.sh unit):
 * no hang past the pipe size, at most 1 MB kept, no fd leak. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
int dmcmd(char *cmd, int n, ...);
void dmcmd_read_alloc(int pipe, char **value);
static int nfds(void) { int n = 0; DIR *d = opendir("/proc/self/fd"); while (readdir(d)) n++; closedir(d); return n; }
int main(void)
{
	const char *sizes[] = {"0", "5", "4096", "4097", "65536", "200000", "3000000", NULL};
	int i, before = nfds();
	for (i = 0; sizes[i]; i++) {
		char sh[128]; char *v = NULL; time_t t0 = time(NULL);
		snprintf(sh, sizeof(sh), "head -c %s /dev/zero | tr '\\000' a", sizes[i]);
		int fd = dmcmd("/bin/sh", 2, "-c", sh);
		dmcmd_read_alloc(fd, &v);
		close(fd);
		printf("asked %8s bytes -> got %8zu, all 'a': %s, %lds\n", sizes[i], strlen(v),
		       strspn(v, "a") == strlen(v) ? "yes" : "NO", (long)(time(NULL) - t0));
	}
	int fd = dmcmd("no-such-command-xyz", 0); char *v = NULL;
	dmcmd_read_alloc(fd, &v); close(fd);
	printf("missing command -> fd %d, '%s'\n", fd, v);
	printf("fds before %d after %d%s\n", before, nfds(), before == nfds() ? "" : " NO");
	return 0;
}
