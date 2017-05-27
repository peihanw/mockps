#include <bits/posix1_lim.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>

#define VERSION_IDENT "@(#)compiled [mockps] at: ["__DATE__"], ["__TIME__"]"
static const char* version_ident = VERSION_IDENT;

#define __file__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)

#define ECHO_TRC(fmt, ...) do{_echo(__file__, __LINE__, __func__, "TRC", fmt, ##__VA_ARGS__);}while(0);
#define ECHO_DBG(fmt, ...) do{_echo(__file__, __LINE__, __func__, "DBG", fmt, ##__VA_ARGS__);}while(0);
#define ECHO_INF(fmt, ...) do{_echo(__file__, __LINE__, __func__, "INF", fmt, ##__VA_ARGS__);}while(0);
#define ECHO_WRN(fmt, ...) do{_echo(__file__, __LINE__, __func__, "WRN", fmt, ##__VA_ARGS__);}while(0);
#define ECHO_ERO(fmt, ...) do{_echo(__file__, __LINE__, __func__, "ERO", fmt, ##__VA_ARGS__);}while(0);

extern char** environ;

static const int _EXIT_0_OK = 0;
static const int _EXIT_1_USAGE = 1;
static const int _EXIT_2_UNIQ = 2;
static const int _EXIT_3_FORK = 3;

static int _Sleep = 0;
static std::string _Mocked;
static bool _Uniq = false;
static int _Verbose = 1;

static std::string _Basename;
static std::string _LockFilePath;
static FILE* _LockFP = NULL;
static int _ArgLen = 0;

void _mainLoop(int argc, char** argv);
void _parseArgs(int argc, char* const* argv);
void _mockCmdline(int argc, char** argv);
void _calcArgLen(int argc, char* const* argv);
void _uniqInstance();
int _tryLock();
void _atExitUnlinkLock();
void _atTerminateUnlinkLock();
void _sigTerminateHandle(int sig);
int _sdbmHash(const char* str, int modulo);
void _dbgArgs();
void _usage(const char* exename, int exit_code);
void _echo(const char* src_file_nm, int src_line_num, const char* func_nm, const char* level, const char* fmt, ...);
void _prefix(const char* src_file_nm, int src_line_num, const char* func_nm, const char* level, std::string& result);
void _sysTimestamp(std::string& result);

int main(int argc, char* const* argv) {
	_parseArgs(argc, argv);
	_mainLoop(argc, (char**)argv);
}

void _mainLoop(int argc, char** argv) {
	pid_t pid_ = fork();

	if (pid_ > 0) {
		ECHO_INF("parent process exit 0");
		exit(_EXIT_0_OK);
	} else if (pid_ == 0) {
		_mockCmdline(argc, argv);
		_uniqInstance();

		if (_Sleep <= 0) {
			while (1) {
				sleep(1);
			}
		} else {
			sleep(_Sleep);
		}

		ECHO_INF("child process exit 0");
		exit(_EXIT_0_OK);
	} else {
		ECHO_ERO("fork failed, errno=%d", errno);
		exit(_EXIT_3_FORK);
	}
}

void _mockCmdline(int argc, char** argv) {
	char* buf_ = new char[_Mocked.length() + 1];
	strcpy(buf_, _Mocked.data());
	_Basename = ::basename(buf_);
	delete[] buf_;
	int end_ = _Basename.find_first_of(' ', 0);

	if (end_ != std::string::npos) {
		_Basename = _Basename.substr(0, end_);
	}

	int len_ = _Basename.length();

	if (len_ > 2 && _Basename.at(len_ - 1) == ':') {
		_Basename = _Basename.substr(0, len_ - 1);
	}

	_calcArgLen(argc, argv);
	char* cp = argv[0];
	memset(cp, '\0', _ArgLen);
	snprintf(cp, _ArgLen - 1, "%s", _Mocked.data());
	prctl(PR_SET_NAME, _Basename.data());
}

void _calcArgLen(int argc, char* const* argv) {
	_ArgLen = 0;
	int env_idx_ = -1;

	while (environ[++ env_idx_]) {
		_ArgLen += strlen(environ[env_idx_]);
	}

	ECHO_TRC("raw _ArgLen = %d", _ArgLen);

	if (_ArgLen) {
		_ArgLen = environ[env_idx_ - 1] + strlen(environ[env_idx_ - 1]) - argv[0];
	} else {
		_ArgLen = argv[argc - 1] + strlen(argv[argc - 1]) - argv[0];
	}

	ECHO_DBG("final _ArgLen = %d", _ArgLen);

	if (_ArgLen < _POSIX_PATH_MAX) {
		ECHO_INF("adjust _ArgLen %d to _POSIX_PATH_MAX %d", _ArgLen, _POSIX_PATH_MAX);
		_ArgLen = _POSIX_PATH_MAX;
	}
}

void _uniqInstance() {
	if (!_Uniq) {
		return;
	}

	int hash_ = _sdbmHash(getenv("HOME"), -1);
	char lock_file_path_[1024];
	sprintf(lock_file_path_, "/tmp/%s.%d.lock", _Basename.data(), hash_);
	_LockFilePath = lock_file_path_;

	if (_tryLock() == 0) {
		return;
	} else {
		exit(_EXIT_2_UNIQ);
	}
}

int _tryLock() {
	int exists_ = 0;
	struct stat stat_;

	if (stat(_LockFilePath.data(), &stat_) == 0) {
		exists_ = 1;
		_LockFP = fopen(_LockFilePath.data(), "r+");
	} else {
		_LockFP = fopen(_LockFilePath.data(), "w");
	}

	if (_LockFP == NULL) {
		ECHO_ERO("fopen(%s), errno=%d\n", _LockFilePath.data(), errno);
		return 1;
	}

	if (lockf(fileno(_LockFP), F_TLOCK, 0) == 0) {
		if (exists_) {
			char buff_[16];
			memset(buff_, '\0', sizeof (buff_));
			fseek(_LockFP, 0L, SEEK_SET);
		}

		fprintf(_LockFP, "%-9d\n", getpid());
		fflush(_LockFP);
		atexit(_atExitUnlinkLock);
		_atTerminateUnlinkLock();
		return 0;
	} else {
		ECHO_WRN("try lock [%s] fail, another instance still running!\n", _LockFilePath.data());
		fclose(_LockFP);
		_LockFP = NULL;
		return 1;
	}
}

void _atExitUnlinkLock() {
	if (!_LockFilePath.empty()) {
		unlink(_LockFilePath.data());
	}
}

void _atTerminateUnlinkLock() {
	if (_LockFilePath.empty()) {
		return;
	}

	struct sigaction new_act_, old_act_;

	new_act_.sa_handler = _sigTerminateHandle;

	sigemptyset(&new_act_.sa_mask);

	new_act_.sa_flags = 0;

	if (sigaction(SIGTERM, &new_act_, &old_act_) < 0) {
		ECHO_WRN("install signal %d handler failed", SIGTERM);
	}

	new_act_.sa_handler = _sigTerminateHandle;
	sigemptyset(&new_act_.sa_mask);
	new_act_.sa_flags = 0;

	if (sigaction(SIGINT, &new_act_, &old_act_) < 0) {
		ECHO_WRN("install signal %d handler failed", SIGINT);
	}
}

void _sigTerminateHandle(int sig) {
	if (!_LockFilePath.empty()) {
		unlink(_LockFilePath.data());
	}

	exit(0);
}

int _sdbmHash(const char* str, int modulo) {
	int hash_ = 0;
	int c;

	if (str == NULL) {
		return 0;
	}

	while ((c = *str++)) {
		hash_ = c + (hash_ << 6) + (hash_ << 16) - hash_;
	}

	if (modulo <= 0) {
		return hash_;
	} else {
		int mod_ = hash_ % modulo;
		return mod_ < 0 ? mod_ + modulo : mod_;
	}
}

void _parseArgs(int argc, char* const* argv) {
	int err_ = 0;
	char c;

	while ((c = getopt(argc, argv, ":s:m:v:u")) != char(EOF)) {
		switch (c) {
		case ':':
			++err_;
			ECHO_ERO("option -%c needs an argument", optopt);
			break;

		case '?':
			++err_;
			ECHO_ERO("unrecognized option -%c", optopt);
			break;

		case 's':
			_Sleep = atoi(optarg);
			break;

		case 'm':
			_Mocked = optarg;
			break;

		case 'u':
			_Uniq = true;
			break;

		case 'v':
			_Verbose = atoi(optarg);
			break;

		default:
			break;
		}
	}

	if (_Verbose < 0) {
		_Verbose = 0;
	} else if (_Verbose > 5) {
		_Verbose = 5;
	}

	_Mocked.erase(0, _Mocked.find_first_not_of(" \t"));
	_Mocked.erase(_Mocked.find_last_not_of(" \t") + 1);

	if (_Mocked.empty()) {
		++err_;
		ECHO_ERO("mocked not specified");
	}

	if (err_) {
		_usage(argv[0], _EXIT_1_USAGE);
	} else {
		_dbgArgs();
	}
}

void _dbgArgs() {
	ECHO_DBG("mocked [%s]", _Mocked.data());
	ECHO_DBG("sleep=%d, uniq=%d, verbose=%d", _Sleep, _Uniq, _Verbose);
}

void _usage(const char* exename, int exit_code) {
	fprintf(stderr, "usage: %s [-s sleep] [-u] [-v verbose] -m \"mocked process with optional args\"\n", exename);
	fprintf(stderr, "       -s : sleep duration in seconds, default 0(infinite)\n");
	fprintf(stderr, "       -u : uniq mocked process, default non-uniq constraint\n");
	fprintf(stderr, "       -v : verbose level, 0:OFF, 1:ERO, 2:WRN, 3:INF, 4:DBG, 5:TRC, default 1\n");
	fprintf(stderr, "       -m : mocked process command line, quoted please\n");
	fprintf(stderr, "eg.    %s -s 60 -u -m \"nginx: master (openresty)\"\n", exename);

	if (exit_code >= 0) {
		exit(exit_code);
	}
}

void _echo(const char* src_file_nm, int src_line_num, const char* func_nm, const char* level, const char* fmt, ...) {
	std::string s;
	_prefix(src_file_nm, src_line_num, func_nm, level, s);

	if (fmt != NULL) {
		char buf_[8192];
		buf_[sizeof(buf_) - 1] = '\0';
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf_, sizeof (buf_) - 1, fmt, ap);
		va_end(ap);
		s.append(buf_);
	}

	if (_Verbose > 0 && strcmp(level, "ERO") == 0) {
		fprintf(stderr, "%s\n", s.data());
	} else {
		if ((_Verbose > 1 && strcmp(level, "WRN") == 0)
			|| (_Verbose > 2 && strcmp(level, "INF") == 0)
			|| (_Verbose > 3 && strcmp(level, "DBG") == 0)
			|| (_Verbose > 4 && strcmp(level, "TRC") == 0)) {
			fprintf(stdout, "%s\n", s.data());
		}
	}
}

void _prefix(const char* src_file_nm, int src_line_num, const char* func_nm, const char* level, std::string& result) {
	char buf_[128];
	_sysTimestamp(result);
	sprintf(buf_, "|%s|%d|%lu|", level, ::getpid(), ::syscall(SYS_gettid));
	result.append(buf_);
	result.append(src_file_nm);
	sprintf(buf_, "|%d|", src_line_num);
	result.append(buf_);
	result.append(func_nm);
	result.push_back('|');
}

void _sysTimestamp(std::string& result) {
	struct timeval tv_;
	struct tm tm_;
	unsigned msec_;
	char buf_[32];
	::gettimeofday(&tv_, NULL);
	msec_ = tv_.tv_usec % 1000000;
	::localtime_r(&tv_.tv_sec, &tm_);
	strftime(buf_, sizeof(buf_), "%y%m%d%H%M%S", &tm_);
	result = buf_;
	sprintf(buf_, ".%06d", msec_);
	result.append(buf_);
}

