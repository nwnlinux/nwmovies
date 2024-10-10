#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <dirent.h>
#include <string.h>
#include <stdarg.h>
#include <pwd.h>

#include "nwmovies.h"

#define NW_PRELOAD_INIT 1
#define NW_PRELOAD_NWUSER 2

static int NWMovies_cleanldpreload()
{
	const char *env = getenv("LD_PRELOAD");
	char *copyenv, *newenv;
	char *token, *ptr;
	int i = 0;
	static int status = 0;

	/* Initilise once only */
	if (status)
		return status;
	else
		status = NW_PRELOAD_INIT;

	if (env == NULL)
		return status;

	if ((copyenv = strdup(env)) == NULL)
		return status;

	if ((newenv = malloc(strlen(env)+1)) == NULL) {
		free(copyenv);
		return status;
	}
	*newenv = '\0';

	token = strtok_r(copyenv, ":", &ptr);
	while (token) {
		if (strstr(token, "nwuser.so"))
			status = NW_PRELOAD_NWUSER;
		if ((strstr(token, "nwuser.so") == NULL) &&
		    (strstr(token, "nwmouse.so") == NULL) &&
		    (strstr(token, "nwmovies.so") == NULL) &&
		    (strstr(token, "nwlogger.so") == NULL)) {
			if (i)
				strcat(newenv, ":");
			else
				i = 1;
			strcat(newenv, token);
		}

		token = strtok_r(NULL, ":", &ptr);
	}

	if (*newenv != '\0') {
		NWMovies_log(0, "NOTICE: NWMovies: Stripped LD_PRELOAD environment \"%s\"\n", newenv);
		setenv("LD_PRELOAD", newenv, 1);
	} else {
		NWMovies_log(0, "NOTICE: NWMovies: Cleared LD_PRELOAD environment\n");
		unsetenv("LD_PRELOAD");
	}

	free(newenv);
	free(copyenv);

	return status;
}

static int NWMovies_skipmovie(const char *movietitle)
{
	FILE *skipfile;
	char skiplist[128];
	char *ptr;
	int status = 0;

	if ((skipfile = fopen("nwmovies.skip", "r"))) {
		while (status == 0 && fgets(skiplist, sizeof(skiplist), skipfile)) {
			if ((ptr = strchr(skiplist, '\n')))
				*ptr = '\0';
			if (!strcasecmp(movietitle, skiplist)) {
				NWMovies_log(0, "NOTICE: NWMovies: Skipped movie \"%s\" as in nwmovies.skip\n", movietitle);
				status = 1;
			}
		}
		fclose(skipfile);
	}
	return status;
}

static char *NWMovies_findmoviefile(const char *movietitle)
{
	DIR *dir;
	struct dirent *entry;
	char tmp[128];
	char *filename = NULL;

	/* Original NWN oonly supports Bink movies */
	snprintf(tmp, 128, "%s.bik" , movietitle);

	/* Current working directory should be game or NWUser override
	 * directory */
	if ((dir = opendir("movies")) == NULL) {
		NWMovies_log(1, "ERROR: NWMovies: No movies directory found\n");
		return NULL;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
			if (!strcasecmp(tmp, entry->d_name)) {
				filename = strdup(entry->d_name);
				break;
			}
		}
	}
	closedir(dir);

	if (filename == NULL)
		NWMovies_log(1, "ERROR: NWMovies: Missing file for movie \"%s\"\n", movietitle);

	return filename;
}

void NWMovies_runcommand(const char *movietitle)
{
	const char *env = getenv("NWMOVIES_PLAY_COMMAND");
	struct passwd *pw = getpwuid(getuid());
	const char *homedir = pw->pw_dir;
	struct stat statbuf;
	char *player, *moviefile;
	char moviepath[256], command[256];
	int nwuser;
	int err;

	/* LD_PRELOAD of NWUser will not work for 64-bit binary in command,
	 * preventing preferential home directory overrides for relative
	 * path returned for filename. Strip LD_PRELOAD of NWHacks.
	 *
	 * NWUser prevents us from finding absolute path from relative path
	 * returned for filename. Try manually stat'ng against absolute
	 * paths which NWUser ignores. Won't work if NWUser is modified
	 * to something other than "$HOME/.nwn"
	 */

	nwuser = NWMovies_cleanldpreload();

	if (NWMovies_skipmovie(movietitle))
		return;

	if (env != NULL)
		player = strdup(env);
	else
		player = strdup(_NWMOVIES_PLAYER);
	if (player == NULL)
		return;

	if ((moviefile = NWMovies_findmoviefile(movietitle)) == NULL) {
		free(player);
		return;
	}

	command[0] = '\0';
	if (nwuser == NW_PRELOAD_NWUSER) {
		snprintf(moviepath, 256, "%s/.nwn/movies/%s", homedir, moviefile);
		/* NWUser override ignores absolute paths */
		if (!stat(moviepath, &statbuf))
			if (S_ISREG(statbuf.st_mode))
				snprintf(command, 256, "%s %s", player, moviepath);
	}
	if (command[0] == '\0')
		snprintf(command, 256, "%s movies/%s", player, moviefile);

	NWMovies_log(0, "NOTICE: NWMovies: Running command \"%s\"\n", command);
	err = system(command);
	if (err)
		NWMovies_log(0, "WARNING: NWMovies: Command returned error %d\n", WEXITSTATUS(err));

	free(player);
	free(moviefile);
}
