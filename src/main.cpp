#include <string>
#include <iostream>
#include "SDLVideoPlayer.h"

int main(int argc, char *argv[])
{

	if (argc != 2)
	{
		std::cout << "Usage: SDLVideoPlayer filename" << std::endl;
		return 0;
	}

	SDLVideoPlayer player;
	player.StartPlay(argv[1]);
	system("PAUSE");
	player.StopPlay();
	return 0;
}