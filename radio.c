#include "minigbs.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

static int sock_notes;
static int sock_ctrl;

static int poll_idx_notes;
static int poll_idx_ctrl;

#define PATH_NOTES "\0minigbs_notes"
#define PATH_CTRL  "\0minigbs_ctrl"

static struct sockaddr_un notes_addr = {
	.sun_family = AF_UNIX,
	.sun_path = PATH_NOTES,
};

static struct sockaddr_un ctrl_addr = {
	.sun_family = AF_UNIX,
	.sun_path = PATH_CTRL,
};

static struct client {
	int socket;
	int poll_idx;
	struct client* next;
}* clients;

void radio_init(struct pollfd** fds, int* nfds){

	sock_notes = socket(AF_UNIX, SOCK_STREAM, 0);
	sock_ctrl  = socket(AF_UNIX, SOCK_DGRAM, 0);

	bind(sock_notes, &notes_addr, sizeof(sa_family_t) + sizeof(PATH_NOTES) - 1);
	bind(sock_ctrl,  &ctrl_addr,  sizeof(sa_family_t) + sizeof(PATH_CTRL)  - 1);

	listen(sock_notes, 1);

	*fds = realloc(*fds, (*nfds + 2) * sizeof(struct pollfd));

	poll_idx_notes = (*nfds)++;
	(*fds)[poll_idx_notes] = (struct pollfd){
		.fd = sock_notes,
		.events = POLLIN,
	};

	poll_idx_ctrl = (*nfds)++;
	(*fds)[poll_idx_ctrl] = (struct pollfd){
		.fd = sock_ctrl,
		.events = POLLIN,
	};
}

int radio_event(struct pollfd** fds, int* nfds){
	int rval = 0;

	if((*fds)[poll_idx_ctrl].revents & POLLIN){
		char buf[4096];
		ssize_t n = recv(sock_ctrl, buf, sizeof(buf) - 1, 0);

		if(n > 0){
			buf[n] = 0;
			//printf("got ctrl event: [%.*s]\n", (int)n, buf);

			char* end;
			unsigned long song = strtoul(buf, &end, 10);
			if(end && end != buf && *end == ' '){
				free(cfg.filename);
				cfg.filename = strdup(end + 1);
				cfg.song_no = song;
				rval = 1;
			}
		}
	}

	if((*fds)[poll_idx_notes].revents & POLLIN){
		int fd = accept(sock_notes, NULL, NULL);

		struct client** c = &clients;
		while(*c) c = &(*c)->next;
		*c = malloc(sizeof(struct client));
		**c = (struct client){
			.socket = fd,
			.poll_idx = *nfds,
		};

		*fds = realloc(*fds, (*nfds + 1) * sizeof(struct pollfd));
		(*fds)[(*nfds)++] = (struct pollfd){
			.fd = fd,
			.events = POLLHUP,
		};
	}

	for(struct client** c = &clients; *c; /**/){
		if((*fds)[(*c)->poll_idx].revents & POLLHUP){
			close((*c)->socket);

			struct client* next = (*c)->next;
			free(*c);
			*c = next;
		} else {
			c = &(*c)->next;
		}
	}

	return rval;
}

void radio_frame(void){
	uint16_t notes[4] = {};
	audio_get_notes(notes);

	for(struct client* c = clients; c; c = c->next){
		send(c->socket, notes, sizeof(notes), 0);
	}
}

void radio_client(void){

	cfg.ui_mode = UI_MODE_CHART;
	ui_init();

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(connect(fd, &notes_addr, sizeof(sa_family_t) + sizeof(PATH_NOTES) - 1) == -1){
		perror("connect");
		ui_quit();
		exit(1);
	}

	ssize_t n;
	uint16_t notes[4];

	char* b = (char*)notes;
	char* e = (char*)notes + sizeof(notes);

	do {
		n = read(fd, b, e - b);
		if(n == -1 && errno != EAGAIN){
			perror("read");
		} else if(n && b + n != e){
			b += n;
		} else if(n){
			ui_chart_set(notes);
			ui_chart_draw();
			refresh();
			b = (char*)notes;
		}
	} while(n != 0);

	printf("disconnected\n");
	ui_quit();
	exit(0);
}

void radio_tune(int argc, char** argv){
	int song = argc > 2 ? atoi(argv[2]) : 0;

	char buf[4096];
	snprintf(buf, sizeof(buf), "%d %s", song, argv[1]);

	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	sendto(fd, buf, strlen(buf) + 1, 0, &ctrl_addr, sizeof(sa_family_t) + sizeof(PATH_CTRL) - 1);
}
