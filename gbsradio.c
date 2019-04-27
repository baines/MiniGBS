#include <ncurses.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct song {
	char name[32];
	int  track;
	int  votes;
};

static struct packet {
	struct song songs[3];
	int duration;
	int remaining;
} pkt;

static void redraw(int w, int h){
	static char url[] = "github.com/baines/minigbs";

	mvprintw(0, w/2 - 7, "MiniGBS Radio");
	mvprintw(2, 0, "Type #0, #1 or #2 in chat to select the next song:");

	int maxvote = 0;
	for(int i = 0; i < 3; ++i){
		struct song* s = pkt.songs + i;

		if(s->votes > maxvote)
			maxvote = s->votes;
	}

	for(int i = 0; i < 3; ++i){
		struct song* s = pkt.songs + i;

		char votestr[16];
		memset(votestr, ' ', sizeof(votestr));

		int n = s->votes;
		if(maxvote > 16){
			n = 16 * ((float)s->votes / (float)maxvote);
		}
		memset(votestr, '=', n);

		mvprintw(4+i, 0, " #%d - %-32.32s - T%02d - [%.16s] %d", i, s->name, s->track, votestr, s->votes);
	}

	mvprintw(8, w - sizeof(url) + 1, url);

	char timestr[32];
	memset(timestr, ' ', sizeof(timestr));

	int n = 32 * (1.0f - ((float)pkt.remaining / (float)pkt.duration));
	memset(timestr, '=', n);

	mvprintw(8, 0, "[%02d:%02d] [%.32s]", pkt.remaining / 60, pkt.remaining % 60, timestr);

	refresh();
}

static int fd = -1;

static void reconnect(void){
#define PATH_INSOBOT "\0insobot_gbsradio"
	static const struct sockaddr_un addr_insobot = {
		.sun_family = AF_UNIX,
		.sun_path = PATH_INSOBOT,
	};

	if(fd != -1){
		close(fd);
	}

	fd = socket(AF_UNIX, SOCK_STREAM, 0);

	while(connect(fd, &addr_insobot, sizeof(sa_family_t) + sizeof(PATH_INSOBOT) - 1) == -1){
		//perror("connect");
		usleep(2 * 1000 * 1000);
	}
}

static void recv_cmd(){
	char buf[4096];

	ssize_t n = recv(fd, buf, sizeof(buf), 0);
	if(n < 0){
		//perror("recv");
	} else if(n >= sizeof(pkt)){
		memcpy(&pkt, buf, sizeof(pkt));
	} else if(n == 0){
		reconnect();
	}
}

int main(void){
	initscr();
	noecho();
	cbreak();
	curs_set(0);

	int win_w, win_h;
	getmaxyx(stdscr, win_h, win_w);

	start_color();
	init_color(COLOR_BLACK, 91, 91, 102);
	init_pair(7, COLOR_WHITE, COLOR_BLACK);
	bkgd(COLOR_PAIR(7));

	reconnect();

	for(;;){
		recv_cmd();
		redraw(win_w, win_h);
	}

	endwin();

	return 0;
}
