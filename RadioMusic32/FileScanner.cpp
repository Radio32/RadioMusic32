#include "FileScanner.h"

static int fileNameCompare(const void* a, const void* b) {
    const AudioFileInfo* sa = (const AudioFileInfo*)a;
    const AudioFileInfo* sb = (const AudioFileInfo*)b;
    return sa->name.compareTo(sb->name);
}

FileScanner::FileScanner() {
    lastBankIndex = 0;
    memset(numFiles, 0, sizeof(numFiles));
}

void FileScanner::scan() {
    lastBankIndex = 0;
    memset(numFiles, 0, sizeof(numFiles));

    for (int b = 0; b < BANKS; b++) {
        char bankChar = 'A' + b;

        for (int v = 0; v < VOICES; v++) {
            // Build path "A/1" or "A/2" etc.
            char path[5];
            path[0] = bankChar;
            path[1] = '/';
            path[2] = '1' + v;
            path[3] = '\0';

            File dir = SD.open(path);
            if (!dir || !dir.isDirectory()) {
                if (dir) dir.close();
                continue;
            }

            if (b > lastBankIndex) lastBankIndex = b;

            uint8_t count = 0;
            while (count < MAX_FILES_PER_SLOT) {
                File f = dir.openNextFile(FILE_READ);
                if (!f) break;

                if (!f.isDirectory()) {
                    String fname = f.name();
                    if (fname.endsWith(".wav") || fname.endsWith(".WAV")) {
                        AudioFileInfo& info = fileInfos[b][v][count];
                        if (processWavFile(&f, info)) {
                            // Store full path SD.open() can use: "A/1/01.wav"
                            info.name = String(bankChar) + "/" + String(char('1' + v)) + "/" + fname;
                            info.startPlayFrom = 0;
                            count++;
                        }
                    }
                }
                f.close();
            }
            numFiles[b][v] = count;

            if (count > 1) {
                qsort(&fileInfos[b][v][0], count, sizeof(AudioFileInfo), fileNameCompare);
            }

            dir.close();
        }
    }
}

bool FileScanner::processWavFile(File* f, AudioFileInfo& info) {
    if (!wavHeaderReader.read(f, info)) return false;
    // Accept mono 16-bit 32 kHz or 44.1 kHz
    return info.getChannels() == 1 &&
           info.getBytesPerSample() == 2 &&
           (info.getSampleRate() == 32000 || info.getSampleRate() == 44100);
}
