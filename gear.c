#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>

#pragma comment(lib, "winhttp.lib")

#define PARTS 32
#define BUF_SIZE (4 * 1024 * 1024)

volatile LONG64 downloadedBytes = 0;
ULONGLONG contentLength = 0;

typedef struct {
    wchar_t host[256];
    wchar_t path[1024];
    INTERNET_PORT port;
    BOOL https;
    ULONGLONG start;
    ULONGLONG end;
    int index;
} Job;

LARGE_INTEGER freq, tStart;

// ---------------- URL PARSE ----------------
void parse_url(const wchar_t* url, Job* j) {
    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = j->host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = j->path;
    uc.dwUrlPathLength = 1024;

    WinHttpCrackUrl(url, 0, 0, &uc);

    j->https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    j->port  = j->https ? INTERNET_DEFAULT_HTTPS_PORT
                        : INTERNET_DEFAULT_HTTP_PORT;
}

// ---------------- THREAD ----------------
DWORD WINAPI download_thread(LPVOID p) {
    Job* j = (Job*)p;

    HINTERNET s = WinHttpOpen(L"Gear/FINAL",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        NULL, NULL, 0);

    HINTERNET c = WinHttpConnect(s, j->host, j->port, 0);

    HINTERNET r = WinHttpOpenRequest(
        c, L"GET", j->path,
        NULL, NULL, NULL,
        j->https ? WINHTTP_FLAG_SECURE : 0
    );

    wchar_t range[128];
    swprintf(range, 128, L"Range: bytes=%llu-%llu", j->start, j->end);
    WinHttpAddRequestHeaders(r, range, -1, WINHTTP_ADDREQ_FLAG_ADD);

    WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0);
    WinHttpReceiveResponse(r, NULL);

    wchar_t name[64];
    swprintf(name, 64, L"part_%d.tmp", j->index);
    FILE* f = _wfopen(name, L"wb");

    BYTE* buf = malloc(BUF_SIZE);
    DWORD read;

    while (WinHttpReadData(r, buf, BUF_SIZE, &read) && read) {
        fwrite(buf, 1, read, f);
        InterlockedAdd64(&downloadedBytes, read);
    }

    free(buf);
    fclose(f);

    WinHttpCloseHandle(r);
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return 0;
}

// ---------------- MERGE ----------------
void merge(const wchar_t* out) {
    FILE* o = _wfopen(out, L"wb");
    BYTE* buf = malloc(BUF_SIZE);

    for (int i = 0; i < PARTS; i++) {
        wchar_t name[64];
        swprintf(name, 64, L"part_%d.tmp", i);
        FILE* in = _wfopen(name, L"rb");

        size_t r;
        while ((r = fread(buf, 1, BUF_SIZE, in)))
            fwrite(buf, 1, r, o);

        fclose(in);
        DeleteFileW(name);
    }

    free(buf);
    fclose(o);
}

// ---------------- MAIN ----------------
int main(int argc, char* argv[]) {

    if (argc < 3 || strcmp(argv[1], "install") != 0) {
        printf("Kullanim: gear install <url>\n");
        return 1;
    }

    // ANSI -> Unicode
    wchar_t urlW[2048];
    MultiByteToWideChar(
        CP_UTF8, 0,
        argv[2], -1,
        urlW, 2048
    );

    Job base;
    ZeroMemory(&base, sizeof(base));
    parse_url(urlW, &base);

    wchar_t* fname = wcsrchr(base.path, L'/');
    fname = fname ? fname + 1 : L"output.bin";

    // HEAD request
    HINTERNET s = WinHttpOpen(L"Gear/FINAL",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        NULL, NULL, 0);

    HINTERNET c = WinHttpConnect(s, base.host, base.port, 0);

    HINTERNET r = WinHttpOpenRequest(
        c, L"HEAD", base.path,
        NULL, NULL, NULL,
        base.https ? WINHTTP_FLAG_SECURE : 0
    );

    WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0);
    WinHttpReceiveResponse(r, NULL);

    wchar_t lenStr[64];
    DWORD lenSize = sizeof(lenStr);

    WinHttpQueryHeaders(
        r,
        WINHTTP_QUERY_CONTENT_LENGTH,
        NULL,
        lenStr,
        &lenSize,
        NULL
    );

    contentLength = _wcstoui64(lenStr, NULL, 10);

    WinHttpCloseHandle(r);
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);

    printf("Dosya: %ls (%.2f MB)\n",
        fname,
        contentLength / 1024.0 / 1024.0
    );

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&tStart);

    HANDLE th[PARTS];
    Job jobs[PARTS];
    ULONGLONG partSize = contentLength / PARTS;

    for (int i = 0; i < PARTS; i++) {
        jobs[i] = base;
        jobs[i].index = i;
        jobs[i].start = i * partSize;
        jobs[i].end = (i == PARTS - 1)
            ? contentLength - 1
            : (i + 1) * partSize - 1;

        th[i] = CreateThread(NULL, 0, download_thread, &jobs[i], 0, NULL);
    }

    // Progress
    while (downloadedBytes < contentLength) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        double ms = (now.QuadPart - tStart.QuadPart) * 1000.0 / freq.QuadPart;
        double percent = (double)downloadedBytes * 100.0 / contentLength;
        double speed = (downloadedBytes / 1024.0 / 1024.0) / (ms / 1000.0);

        int bar = (int)(percent / 100.0 * 30);

        printf(
            "\r[%-30.*s] %5.1f%% | %.2f MB/s | %.0f ms",
            bar,
            "==============================",
            percent,
            speed,
            ms
        );
        fflush(stdout);
        Sleep(50);
    }

    WaitForMultipleObjects(PARTS, th, TRUE, INFINITE);
    merge(fname);

    printf("\nIndirme tamamlandi.\n");
    return 0;
}
}
