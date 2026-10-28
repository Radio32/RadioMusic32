#ifndef FileScanner_h
#define FileScanner_h

#include <SD.h>
#include "AudioFileInfo.h"
#include "WavHeaderReader.h"

#define BANKS               16  // A–P
#define VOICES              2   // 0 = sampler, 1 = chromatic
#define MAX_FILES_PER_SLOT  16

class FileScanner {
public:
    FileScanner();
    void scan();

    AudioFileInfo fileInfos[BANKS][VOICES][MAX_FILES_PER_SLOT];
    uint8_t numFiles[BANKS][VOICES];
    uint8_t lastBankIndex;

private:
    bool processWavFile(File* f, AudioFileInfo& info);
    WavHeaderReader wavHeaderReader;
};

#endif
