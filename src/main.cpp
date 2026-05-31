#include <iostream>
#include "MediaPlayer.h"
#include <windows.h>

bool enableANSISupport() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    return SetConsoleMode(hOut, dwMode);
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cerr << "\033[32mПривіт!\033[0m\n" << argv[0] << " - Це простий відеоплеєр, що використовує FFmpeg та SDL2\n"
        << "\033[4mВикористання:\033[0m " << argv[0] << " <video_file>\n"
        << "\n"
        << " * \033[4;33mКомбінації клавіш (органи керування)\033[0m:\n"
        << "\033[1mПробіл\033[0m - \033[36mПлей / Пауза\033[0m\n"
        << "\033[1mСтрілочки вліво / вправо\033[0m - \033[36mПеремотка -10 / +10 секунд\033[0m\n"
        << "\033[1mСтрілочки вверх / вниз\033[0m - \033[36mЗміна гучності +10 / -10\033[0m\n"
        << "\033[1mM\033[0m - \033[36mвимкнення / ввімкнення звуку\033[0m\n"
        << "\033[1mF\033[0m - \033[36mперехід у повноекранний режим\033[0m\n"
        << "\033[1mEsc\033[0m - \033[36mвихід\033[0m\n"
	    << "\n";

    std::cout << "VideoPlayer – FFmpeg + SDL2\n"  << "\033[4mВідкриття:\033[0m \n" << argv[1] << "\n" << "";

    MediaPlayer player(argv[1]);
    return player.run() ? 0 : 1;
}