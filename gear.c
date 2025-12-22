#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>

#define MAX_RESULTS 20
#define LINE 512

char results[MAX_RESULTS][LINE];
int result_count = 0;

void set_color(int c) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

void banner() {
    set_color(14);
    printf("GEAR PACKAGE MANAGER\n\n");
    set_color(7);
}

void run_search(const char* query) {
    char cmd[512];
    sprintf(cmd,
        "winget search %s > %%TEMP%%\\gear_search.txt",
        query
    );
    system(cmd);
}

void load_results() {
    FILE* f = fopen(getenv("TEMP"), "r");
    char path[MAX_PATH];
    sprintf(path, "%s\\gear_search.txt", getenv("TEMP"));

    f = fopen(path, "r");
    if (!f) return;

    char line[LINE];
    while (fgets(line, LINE, f) && result_count < MAX_RESULTS) {
        if (strstr(line, "---") || strlen(line) < 5) continue;
        strcpy(results[result_count++], line);
    }
    fclose(f);
}

int menu() {
    int sel = 0, ch;

    while (1) {
        system("cls");
        banner();

        for (int i = 0; i < result_count; i++) {
            if (i == sel) {
                set_color(10);
                printf("> %s", results[i]);
                set_color(7);
            } else {
                printf("  %s", results[i]);
            }
        }

        ch = _getch();
        if (ch == 72 && sel > 0) sel--;
        if (ch == 80 && sel < result_count - 1) sel++;
        if (ch == 13) return sel;
    }
}

void install_selected(int idx) {
    char id[128];
    sscanf(results[idx], "%*s %127s", id);

    char cmd[256];
    sprintf(cmd, "winget install --id %s -e", id);

    set_color(10);
    printf("\nInstalling %s...\n", id);
    set_color(7);

    system(cmd);
}

int main(int argc, char* argv[]) {
    if (argc < 3 || strcmp(argv[1], "install") != 0) {
        banner();
        printf("Usage: gear install <package>\n");
        return 0;
    }

    run_search(argv[2]);
    load_results();

    if (result_count == 0) {
        printf("No packages found.\n");
        return 0;
    }

    int choice = menu();
    install_selected(choice);

    return 0;
}
