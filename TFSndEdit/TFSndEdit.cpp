// Konami Yu-Gi-Oh! Tag Force SNDDAT repacker
// by Xan/Tenjoin

#if defined (_WIN32) || defined (_WIN64)
#include "stdafx.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "crc32.h"
#include "includes\mini\ini.h"
using namespace mINI;
using namespace std;

#define SNDBNK_TYPE_AT3 0
#define SNDBNK_TYPE_VAG 1

#define ADSR_ENTRY_SIZE_TF1 0x10
#define ADSR_ENTRY_SIZE 0x18

#ifdef __GNUC__
#define path_separator_char '/'
#define path_separator_str "/"
#endif

void* EntryBuffer;
unsigned int AT3FileSize;

char OutFileName[512];
char DefFileName[512];
char OutPath[512];
char OutPathSHDS[512];
char MkDirString[512];
char EntryName[17];

char AudioFilename[512];

// pack mode stuff
char** FileDirectoryListing;
unsigned int FileCount;
char WorkFileName[512];

int cur_idx;

struct snddat_entry
{
    int16_t StartPointerMagic; // always SP or 0x5053
    int16_t Index;
    int32_t EntrySize;
    int32_t HeaderSize;
    int32_t DataStartPointer; // same as AT3DataStartPointer when VAG doesn't exist (in new variants only), zero in BGP
    int32_t VAGDataStartPointer; // same as AT3DataStartPointer when VAG doesn't exist (in new variants only), zero in BGP
    int32_t AT3DataStartPointer; // TODO: what happens if AT3 doesn't exist?
    int32_t VAGADSRDataPointer; // zero in BGP, different in data type 1, each element is 16 bytes big...
    int32_t DataEndPointer; // zero in BGP
}Entry;

struct snddat_subentry
{
    int16_t unk1; // seems to be always 0x4064 or 0x407F, 0x4030 for BGP, ADSR? -- affects volume
    int16_t unk2; // sometimes 5 for voices in tf6, type?
    int16_t unk3;
    int16_t DataType; // 0 for AT3, 1 for VAG
    int32_t offset; // offset relative to (type)DataStartPointer, AT3 points straight to data, VAG points to an offset pair for data + ADSR
    int32_t unk4; // 0
    int32_t unk5; // 0
    int32_t unk6; // 0
    int32_t unk7; // this number is unique for every sound, seems to increase with size? offset? fade time?
    int32_t unk8; // 0
};

struct vagadsr
{
    int16_t unk1;
    int16_t unk2;
    int16_t unk3;
    int16_t unk4;
    int16_t unk5;
    int16_t unk6;
    int16_t unk7;
    int16_t unk8;
};

struct vagadsr_ext
{
    int16_t unk9;
    int16_t unk10;
    int16_t unk11;
    int16_t unk12;
}adsr_ext_dummy;

struct vagoffsetpair
{
    int32_t offset; // relative to VAGDataStartPointer
    int32_t adsr_offset; // relative to VAGADSRDataPointer
};

int ADSREntrySize = ADSR_ENTRY_SIZE;

// SHDS stuff
void* SHDSbuffer;
int16_t SHDSver;
int32_t* SHDSoffsets;
struct LoadPacAddr
{
    int32_t dummy1;
    int32_t dummy2;
    int32_t size;
    int32_t dummy3;
}*SHDSsizes;
unsigned int PacCount;
unsigned int SHDStotalsize;
bool bTF6mode;
bool bTF1mode;
unsigned int TF6voicepacoffset;

// ELF stuff
long ElfSHDSOffset = 0;
long ElfSHDSSize = 0;
bool bElfMode = false;
void* ElfChunkTop;
void* ElfChunkBottom;
int32_t ElfBottomSize;

#ifdef WIN32
DWORD GetDirectoryListing(const char* FolderPath)
{
    WIN32_FIND_DATA ffd = { 0 };
    TCHAR  szDir[MAX_PATH];
    char MBFilename[MAX_PATH];
    HANDLE hFind = INVALID_HANDLE_VALUE;
    DWORD dwError = 0;
    unsigned int NameCounter = 0;

    mbstowcs(szDir, FolderPath, MAX_PATH);
    StringCchCat(szDir, MAX_PATH, TEXT("\\*"));

    if (strlen(FolderPath) > (MAX_PATH - 3))
    {
        _tprintf(TEXT("Directory path is too long.\n"));
        return -1;
    }

    hFind = FindFirstFile(szDir, &ffd);

    if (INVALID_HANDLE_VALUE == hFind)
    {
        printf("FindFirstFile error\n");
        return dwError;
    }

    // count the files up first
    do
    {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            wcstombs(MBFilename, ffd.cFileName, MAX_PATH);
            if ((strstr(MBFilename, ".ini") != 0) || (strstr(MBFilename, ".vag") != 0))
                FileCount++;
        }
    } while (FindNextFile(hFind, &ffd) != 0);

    dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES)
    {
        printf("FindFirstFile error\n");
    }
    FindClose(hFind);

    // then create a file list in an array, redo the code
    FileDirectoryListing = (char**)calloc(FileCount, sizeof(char*));

    ffd = { 0 };
    hFind = FindFirstFile(szDir, &ffd);
    if (INVALID_HANDLE_VALUE == hFind)
    {
        printf("FindFirstFile error\n");
        return dwError;
    }

    do
    {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            wcstombs(MBFilename, ffd.cFileName, MAX_PATH);
            if ((strstr(MBFilename, ".ini") != 0) || (strstr(MBFilename, ".vag") != 0))
            {
                FileDirectoryListing[NameCounter] = (char*)calloc(strlen(MBFilename) + 1, sizeof(char));
                strcpy(FileDirectoryListing[NameCounter], MBFilename);
                NameCounter++;
            }
        }
    } while (FindNextFile(hFind, &ffd) != 0);

    dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES)
    {
        printf("FindFirstFile error\n");
    }

    FindClose(hFind);
    return dwError;
}
#else

#if __GNUC__
void GetDirectoryListing(const char* FolderPath)
{
    struct dirent* dp;
    DIR* dir = opendir(FolderPath);
    unsigned int NameCounter = 0;

    // count the files up first
    while ((dp = readdir(dir)))
    {
        // ignore the current and previous dir files...
        if (!((strcmp(dp->d_name, ".") == 0) || (strcmp(dp->d_name, "..") == 0)))
        {
            if ((strstr(MBFilename, ".ini") != 0) || (strstr(MBFilename, ".vag") != 0))
            {
                FileCount++;
            }
        }
    }
    closedir(dir);

    // then create a file list in an array, redo the code
    FileDirectoryListing = (char**)calloc(FileCount, sizeof(char*));

    dir = opendir(FolderPath);
    while ((dp = readdir(dir)))
    {
        // ignore the current and previous dir files...
        if (!((strcmp(dp->d_name, ".") == 0) || (strcmp(dp->d_name, "..") == 0)))
        {
            if ((strstr(MBFilename, ".ini") != 0) || (strstr(MBFilename, ".vag") != 0))
            {
                FileDirectoryListing[NameCounter] = (char*)calloc(strlen(dp->d_name) + 1, sizeof(char));
                strcpy(FileDirectoryListing[NameCounter], dp->d_name);
                NameCounter++;
            }
        }
    }
    closedir(dir);
}
#endif

#endif

struct VAGHeader
{
    uint32_t magic = 0x70474156;
    uint32_t bitdepth = 0x20000000;
    uint32_t pad1;
    uint32_t size;
    uint16_t pad2;
    uint16_t rate;
    uint8_t pad[0xC];
    uint8_t name[16];
}vag_head;

void GenerateVAGHeader(uint32_t size, uint16_t rate, uint8_t* name)
{
    vag_head.size = _byteswap_ulong(size);
    vag_head.rate = _byteswap_ushort(rate);
    memcpy(vag_head.name, name, 16);
}

unsigned int FindMinIniNumber()
{
    unsigned int ReadNum = 0;
    unsigned int SmallestNum = 0xFFFFFFFF;

    for (unsigned int i = 0; i < FileCount; i++)
    {
        sscanf(FileDirectoryListing[i], "%d.ini", &ReadNum);
        if (ReadNum < SmallestNum)
            SmallestNum = ReadNum;
    }
    return SmallestNum;
}

unsigned int FindMaxIniNumber()
{
    unsigned int ReadNum = 0;
    unsigned int BiggestNum = 0;

    for (unsigned int i = 0; i < FileCount; i++)
    {
        sscanf(FileDirectoryListing[i], "%d.ini", &ReadNum);
        if (ReadNum > BiggestNum)
            BiggestNum = ReadNum;
    }

    return BiggestNum;
}

uint16_t GetVAGUnkNum(int idx, int subidx)
{
    // search for appropriate VAG from the file dir list instead... we do this to read the unknown number appended before the extension
    char searchstr[16];
    int dl_idx = 0;
    uint16_t unk_vag_val = 0;
    uint32_t dummy = 0;
    sprintf(searchstr, "%d_%d_", idx, subidx);
    for (int y = 0; y < FileCount; y++)
    {
        if (strstr(FileDirectoryListing[y], searchstr))
        {
            dl_idx = y;
            break;
        }
    }
    sscanf(FileDirectoryListing[dl_idx], "%d_%d_%hd.vag", &dummy, &dummy, &unk_vag_val);
    return unk_vag_val;
}

bool bFileExists(const char* Filename)
{
    FILE* chk = fopen(Filename, "rb");
    if (!chk)
        return false;
    fclose(chk);
    return true;
}

int ExtractSubEntries(const char* OutFilePath)
{
    FILE* OutFile;
    int CurrentDataSize = 0;
    int CurrentDataOffset = 0;

    sprintf(DefFileName, "%s/%d.ini", OutFilePath, Entry.Index);
    FILE* DefFile = fopen(DefFileName, "wb");
    if (DefFile == NULL)
    {
        printf("ERROR: Error opening file for writing: %s\n", DefFileName);
        perror("ERROR");
        return -1;
    }

    unsigned int SubEntryCount = 0;

    if (Entry.DataStartPointer == 0)
        SubEntryCount = (Entry.AT3DataStartPointer - Entry.HeaderSize) / 0x20;
    else
        SubEntryCount = (Entry.DataStartPointer - Entry.HeaderSize) / 0x20;

    // store the entry's name (if it exists at all)
    if (Entry.HeaderSize == 0x30)
    {
        strncpy(EntryName, (const char*)((int)EntryBuffer + 0x20), 16);
        fprintf(DefFile, "[Entry]\nName = %s\nCount = %d\n", EntryName, SubEntryCount);
    }
    else
        fprintf(DefFile, "[Entry]\nName = NONE\nCount = %d\n", SubEntryCount);


    // create a subentry object
    snddat_subentry* subentries = (snddat_subentry*)((int)EntryBuffer + Entry.HeaderSize);

    // create VAG offset object
    vagoffsetpair* vagoffsets = (vagoffsetpair*)((int)EntryBuffer + Entry.DataStartPointer);

    // extract bank definitions
    for (int i = 0; i < SubEntryCount; i++)
    {
       fprintf(DefFile, "[%d]\nunk1 = %hd\nunk2 = %hd\nunk3 = %hd\nunk4 = %d\nunk5 = %d\nunk6 = %d\nunk7 = %d\nunk8 = %d\n", i, subentries[i].unk1, subentries[i].unk2, subentries[i].unk3, subentries[i].unk4, subentries[i].unk5, subentries[i].unk6, subentries[i].unk7, subentries[i].unk8);
       
       if (subentries[i].DataType == SNDBNK_TYPE_VAG)
           fprintf(DefFile, "VAG_ADSR_index = %d\n", vagoffsets[subentries[i].offset / 8].adsr_offset / ADSREntrySize);
       else
           fprintf(DefFile, "VAG_ADSR_index = -1\n");
    }

    // also extract VAG ADSRs
    // this should actually be stored in the .vag file that it extracts and refers to, but that would require constructing a VAG header (much like the GIM header for card images)
    // for the purposes of repacking/editing, right now, it isn't necessary

    // check first if this entry contains a VAG at all by checking data pointers (or it could be done by data end pointers, whatever works)
    if (Entry.VAGDataStartPointer != Entry.AT3DataStartPointer)
    {
        unsigned int ADSRCount = (Entry.DataEndPointer - Entry.VAGADSRDataPointer) / ADSREntrySize;
        fprintf(DefFile, "[VAGADSR]\nCount = %d\n", ADSRCount);

        vagadsr* ADSRbase = (vagadsr*)((int)EntryBuffer + Entry.VAGADSRDataPointer);
        vagadsr_ext* ADSR_ext = &adsr_ext_dummy;
        unsigned int asdr_cursor = 0;
        vagadsr* ADSRs = ADSRbase;

        for (int i = 0; i < ADSRCount; i++)
        {
            asdr_cursor = ADSREntrySize * i;
            ADSRs = (vagadsr*)(((int)ADSRbase) + asdr_cursor);

            fprintf(DefFile, "ADSR_%d_unk1 = 0x%hX\n", i, ADSRs->unk1);
            fprintf(DefFile, "ADSR_%d_unk2 = 0x%hX\n", i, ADSRs->unk2);
            fprintf(DefFile, "ADSR_%d_unk3 = 0x%hX\n", i, ADSRs->unk3);
            fprintf(DefFile, "ADSR_%d_unk4 = 0x%hX\n", i, ADSRs->unk4);
            fprintf(DefFile, "ADSR_%d_unk5 = 0x%hX\n", i, ADSRs->unk5);
            fprintf(DefFile, "ADSR_%d_unk6 = 0x%hX\n", i, ADSRs->unk6);
            fprintf(DefFile, "ADSR_%d_unk7 = 0x%hX\n", i, ADSRs->unk7);
            fprintf(DefFile, "ADSR_%d_unk8 = 0x%hX\n", i, ADSRs->unk8);
            if (!bTF1mode)
                ADSR_ext = (vagadsr_ext*)(((int)ADSRs) + ADSR_ENTRY_SIZE_TF1);
            fprintf(DefFile, "ADSR_%d_unk9 = 0x%hX\n", i, ADSR_ext->unk9);
            fprintf(DefFile, "ADSR_%d_unk10 = 0x%hX\n", i, ADSR_ext->unk10);
            fprintf(DefFile, "ADSR_%d_unk11 = 0x%hX\n", i, ADSR_ext->unk11);
            fprintf(DefFile, "ADSR_%d_unk12 = 0x%hX\n", i, ADSR_ext->unk12);
        }
    }
    else
        fprintf(DefFile, "[VAGADSR]\nCount = 0\n");

    fclose(DefFile);

    // extract files
    for (int i = 0; i < SubEntryCount; i++)
    {
        if (subentries[i].DataType == SNDBNK_TYPE_VAG)
        {
            CurrentDataOffset = Entry.VAGDataStartPointer + vagoffsets[subentries[i].offset / 8].offset + 4;
            CurrentDataSize = *(int32_t*)((int)EntryBuffer + (CurrentDataOffset - 4)) + 4;
            uint16_t VAGunkparam = *(uint16_t*)((int)EntryBuffer + CurrentDataOffset);
            sprintf(OutFileName, "%s/%d_%d_%hd.vag", OutFilePath, Entry.Index, i, VAGunkparam);
        }
        else
        {
            CurrentDataOffset = Entry.AT3DataStartPointer + subentries[i].offset + 4;
            CurrentDataSize = *(int32_t*)((int)EntryBuffer + Entry.AT3DataStartPointer + subentries[i].offset);
            sprintf(OutFileName, "%s/%d_%d.at3", OutFilePath, Entry.Index, i);
        }

        printf("EXTRACTING: [%d | %d/%d] %s (@0x%X size: 0x%X)\n", Entry.Index, i + 1, SubEntryCount, OutFileName, CurrentDataOffset, CurrentDataSize);
        
        OutFile = fopen(OutFileName, "wb");
        if (OutFile == NULL)
        {
            printf("ERROR: Error opening file for writing: %s\n", OutFilePath);
            perror("ERROR");
            return -1;
        }
        if (subentries[i].DataType == SNDBNK_TYPE_VAG)
        {
            uint8_t VAGname[16];
            memset(VAGname, 0, 16);
            sprintf((char*)VAGname, "%d_%d", Entry.Index, i);
            GenerateVAGHeader(CurrentDataSize - 4, *(uint16_t*)((int)EntryBuffer + CurrentDataOffset + 2), VAGname);
            fwrite(&vag_head, sizeof(VAGHeader), 1, OutFile);
            fwrite((void*)((int)EntryBuffer + CurrentDataOffset + 4), CurrentDataSize - 4, 1, OutFile);
        }
        else
        {
            fwrite((void*)((int)EntryBuffer + CurrentDataOffset), CurrentDataSize, 1, OutFile);
        }

        fclose(OutFile);
    }

    
    return 0;
}

int PackEntry(char* InFilename, char* OutFilename, FILE* OutFile)
{
    //FILE* OutFile;
    FILE* InFile;
    struct stat st = { 0 };
    long FilePos = 0;

    // check for ini file existence, reusing the InFile pointer...
    InFile = fopen(InFilename, "rb");
    if (InFile == NULL)
    {
        printf("ERROR: Error opening file for reading: %s\n", InFilename);
        perror("ERROR");
        return -1;
    }
    fclose(InFile);

    INIFile inifile(InFilename);
    INIStructure entryini;
    inifile.read(entryini);

    unsigned int SubEntryCount = 0;
    unsigned int vag_count = 0;
    unsigned int at3_count = 0;
    unsigned int adsr_count = 0;

    unsigned int at3_cursor = 0;
    unsigned int vag_cursor = 0;
    unsigned int data_cursor = 0;
    unsigned int j = 0;

    // DUPLICATE TRACKER STUFF!
    // subentries (or rather, its data) are duplicated in the soundbank, so we have to detect what is a duplicate of what and mark it accordingly
    // this is going to be done over a simple int array because it's complicated enough already...
    unsigned int* crc32sums;
    int* se_duplicateof; // subentry duplicateof, if not a duplicate of anything, it's less than zero

    // trailer stuff
    int32_t AlignedEnd;

    char IniSectionGen[14] = {0};
    char* FileExtPoint;

    void* AudioFileBuffer;
    
    // start generating the header...
    snddat_entry GenEntry = {0};
    snddat_subentry* Gen_subentries = NULL;
    vagoffsetpair* Gen_vagoffsets = NULL;
    vagadsr* Gen_ADSRs = NULL;
    vagadsr_ext* Gen_ADSR_ext = NULL;

    // set start magic...
    GenEntry.StartPointerMagic = 0x5053;

    // get the index number from input filename...
    // check if it's a path string
    char* path_detect = strrchr(InFilename, path_separator_char) + 1;
    if (path_detect != NULL)
        sscanf(path_detect, "%hd.ini", &GenEntry.Index);
    else
        sscanf(InFilename, "%hd.ini", &GenEntry.Index);

    strncpy(EntryName, entryini["Entry"]["Name"].c_str(), 16);

    if (strcmp(EntryName, "NONE") == 0)
        GenEntry.HeaderSize = 0x20;
    else
        GenEntry.HeaderSize = 0x30;

    SubEntryCount = stoi(entryini["Entry"]["Count"]);

    // create subentry objects...
    Gen_subentries = (snddat_subentry*)calloc(SubEntryCount, sizeof(snddat_subentry));
    crc32sums = (unsigned int*)calloc(SubEntryCount, sizeof(int));
    se_duplicateof = (int*)calloc(SubEntryCount, sizeof(int));

    // each subentry definition is 0x20 bytes long
    GenEntry.DataStartPointer = (SubEntryCount * sizeof(snddat_subentry)) + GenEntry.HeaderSize;

    // start parsing some data... 
    // subentry defs...
    for (int i = 0; i < SubEntryCount; i++)
    {
        sprintf(IniSectionGen, "%d", i);

        Gen_subentries[i].unk1 = (int16_t)stoi(entryini[IniSectionGen]["unk1"]);
        Gen_subentries[i].unk2 = (int16_t)stoi(entryini[IniSectionGen]["unk2"]);
        Gen_subentries[i].unk3 = (int16_t)stoi(entryini[IniSectionGen]["unk3"]);
        Gen_subentries[i].unk4 = stoi(entryini[IniSectionGen]["unk4"]);
        Gen_subentries[i].unk5 = stoi(entryini[IniSectionGen]["unk5"]);
        Gen_subentries[i].unk6 = stoi(entryini[IniSectionGen]["unk6"]);
        Gen_subentries[i].unk7 = stoi(entryini[IniSectionGen]["unk7"]);
        Gen_subentries[i].unk8 = stoi(entryini[IniSectionGen]["unk8"]);

        // count up the vags by the ADSR index (if it's unused, it's not a VAG)
        if (stoi(entryini[IniSectionGen]["VAG_ADSR_index"]) >= 0)
        {
            Gen_subentries[i].DataType = 1;
            vag_count++;
        }
        else
            at3_count++;
    }

    if (vag_count)
    {
        // create vag objects...
        Gen_vagoffsets = (vagoffsetpair*)calloc(vag_count, sizeof(vagoffsetpair));
        Gen_ADSRs = (vagadsr*)calloc(vag_count, sizeof(vagadsr));
        Gen_ADSR_ext = (vagadsr_ext*)calloc(vag_count, sizeof(vagadsr_ext));

        GenEntry.VAGDataStartPointer = (vag_count * sizeof(vagoffsetpair)) + GenEntry.DataStartPointer;
        // load up the ADSRs
        adsr_count = stoi(entryini["VAGADSR"]["Count"]);
        for (int i = 0; i < adsr_count; i++)
        {
            sprintf(IniSectionGen, "ADSR_%d_unk1", i);
            Gen_ADSRs[i].unk1 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk2", i);
            Gen_ADSRs[i].unk2 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk3", i);
            Gen_ADSRs[i].unk3 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk4", i);
            Gen_ADSRs[i].unk4 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk5", i);
            Gen_ADSRs[i].unk5 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk6", i);
            Gen_ADSRs[i].unk6 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk7", i);
            Gen_ADSRs[i].unk7 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk8", i);
            Gen_ADSRs[i].unk8 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk9", i);
            Gen_ADSR_ext[i].unk9 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk10", i);
            Gen_ADSR_ext[i].unk10 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk11", i);
            Gen_ADSR_ext[i].unk11 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);

            sprintf(IniSectionGen, "ADSR_%d_unk12", i);
            Gen_ADSR_ext[i].unk12 = (int16_t)stoi(entryini["VAGADSR"][IniSectionGen], 0, 16);
        }
    }

    // PRECALCULATE SIZES -- this step is necessary to ensure proper generation of headers...
    for (int i = 0; i < SubEntryCount; i++)
    {
        strcpy(AudioFilename, InFilename);
        FileExtPoint = strrchr(AudioFilename, '.');

        if (Gen_subentries[i].DataType == SNDBNK_TYPE_VAG)
        {
            uint16_t unk_vag_val = GetVAGUnkNum(cur_idx, i);
            sprintf(FileExtPoint, "_%d_%hd.vag", i, unk_vag_val);
            sprintf(IniSectionGen, "%d", i);
            if (stat(AudioFilename, &st))
            {
                printf("ERROR: Can't find %s during size calculation!\n", AudioFilename);
                return -1;
            }
            Gen_subentries[i].offset = j * sizeof(vagoffsetpair);
            Gen_vagoffsets[j].offset = vag_cursor;
            Gen_vagoffsets[j].adsr_offset = stoi(entryini[IniSectionGen]["VAG_ADSR_index"]) * ADSREntrySize;
            vag_cursor = vag_cursor + (st.st_size - 0x30) + 8;
            j++;
        }
        else
        {
            sprintf(FileExtPoint, "_%d.at3", i);
            if (stat(AudioFilename, &st))
            {
                printf("ERROR: Can't find %s during size calculation!\n", AudioFilename);
                return -1;
            }

            // LOAD FILE TO CALC CRC32
            InFile = fopen(AudioFilename, "rb");
            if (InFile == NULL)
            {
                printf("ERROR: Error opening file for reading: %s\n", AudioFilename);
                perror("ERROR");
                return -1;
            }
            // read file to memory
            AudioFileBuffer = malloc(st.st_size);
            fread(AudioFileBuffer, st.st_size, 1, InFile);
            fclose(InFile);

            crc32sums[i] = crc32c(0, (unsigned char*)AudioFileBuffer, st.st_size);

            free(AudioFileBuffer);

            // compare every new file with all previous ones for duplicates and mark them
            if (i)
            {
                for (int x = 0; x < i; x++)
                {
                    se_duplicateof[i] = -1;
                    if (crc32sums[i] == crc32sums[x])
                    {
                        se_duplicateof[i] = x;
                        break;
                    }
                }
            }
            else
                se_duplicateof[i] = -1;
            if (se_duplicateof[i] >= 0)
            {
                Gen_subentries[i].offset = Gen_subentries[se_duplicateof[i]].offset;
            }
            else
            {
                Gen_subentries[i].offset = at3_cursor;
                at3_cursor = at3_cursor + st.st_size + 4;
            }
        }
    }

    if (vag_count)
    {
        GenEntry.AT3DataStartPointer = vag_cursor + GenEntry.VAGDataStartPointer;
        GenEntry.VAGADSRDataPointer = GenEntry.AT3DataStartPointer + at3_cursor;
        GenEntry.DataEndPointer = GenEntry.VAGADSRDataPointer + (adsr_count * ADSREntrySize);
        AlignedEnd = (GenEntry.DataEndPointer + 0x800) & 0xFFFFF800;
    }
    else
    {
      //  if (GenEntry.HeaderSize == 0x30)
      //  {
            GenEntry.AT3DataStartPointer = GenEntry.DataStartPointer;
            GenEntry.VAGDataStartPointer = GenEntry.DataStartPointer;
            GenEntry.VAGADSRDataPointer = GenEntry.DataStartPointer + at3_cursor;
            GenEntry.DataEndPointer = GenEntry.VAGADSRDataPointer;
      //  }
      //  else
      //  {
      //      GenEntry.AT3DataStartPointer = GenEntry.DataStartPointer;
       //     GenEntry.DataStartPointer = 0;

      //  }
        AlignedEnd = (GenEntry.AT3DataStartPointer + at3_cursor + 0x800) & 0xFFFFF800;
    }

    GenEntry.EntrySize = AlignedEnd;

    // start writing to file...
    FilePos = ftell(OutFile);
    // update SHDS info
    SHDSoffsets[GenEntry.Index] = FilePos;
    if (bTF6mode)
        SHDSoffsets[GenEntry.Index] = SHDSoffsets[GenEntry.Index] + TF6voicepacoffset;
    SHDSsizes[GenEntry.Index].size = GenEntry.EntrySize;
    // write header
    fwrite(&GenEntry, sizeof(snddat_entry), 1, OutFile);
    // if we're working with the new type with a name, write it in
    if (GenEntry.HeaderSize == 0x30)
        fwrite(EntryName, sizeof(int8_t), 16, OutFile);
    // write subentry defs
    fwrite(Gen_subentries, sizeof(snddat_subentry), SubEntryCount, OutFile);
    // write vag offsets
    if (vag_count)
        fwrite(Gen_vagoffsets, sizeof(vagoffsetpair), vag_count, OutFile);
    // write data...
    // vags take precedence if they exist
    if (vag_count)
    {
        unsigned int vag_size = 0;
        data_cursor = GenEntry.VAGDataStartPointer;
        for (int i = 0; i < SubEntryCount; i++)
        {
            if (Gen_subentries[i].DataType == SNDBNK_TYPE_VAG)
            {
                strcpy(AudioFilename, InFilename);
                FileExtPoint = strrchr(AudioFilename, '.');
                uint16_t unk_vag_num = GetVAGUnkNum(cur_idx, i);
                uint16_t samprate = 0;
                sprintf(FileExtPoint, "_%d_%hd.vag", i, unk_vag_num);
                sprintf(IniSectionGen, "%d", i);

                if (stat(AudioFilename, &st))
                {
                    printf("ERROR: Can't find %s during file writing!\n", AudioFilename);
                    return -1;
                }

                printf("WRITING: [%d | %d/%d] %s (@0x%X size: 0x%X)\n", GenEntry.Index, i + 1, SubEntryCount, AudioFilename, data_cursor, st.st_size);

                InFile = fopen(AudioFilename, "rb");
                if (InFile == NULL)
                {
                    printf("ERROR: Error opening file for reading: %s\n", AudioFilename);
                    perror("ERROR");
                    return -1;
                }
                // read file to memory
                AudioFileBuffer = malloc(st.st_size);
                fread(AudioFileBuffer, st.st_size, 1, InFile);
                fseek(InFile, 0, SEEK_SET);
                fread(&vag_head, sizeof(VAGHeader), 1, InFile);
                fclose(InFile);

                samprate = _byteswap_ushort(vag_head.rate);

                // write size and then the file after it
                vag_size = st.st_size - 0x30;
                fwrite(&vag_size, sizeof(int32_t), 1, OutFile);
                fwrite(&unk_vag_num, sizeof(uint16_t), 1, OutFile);
                fwrite(&samprate, sizeof(uint16_t), 1, OutFile);
                fwrite((void*)((int)AudioFileBuffer + 0x30), st.st_size - 0x30, 1, OutFile);

                // free memory
                free(AudioFileBuffer);

                data_cursor = data_cursor + (st.st_size - 0x30) + 8;
            }
        }
    }
    else
        data_cursor = GenEntry.AT3DataStartPointer;

    for (int i = 0; i < SubEntryCount; i++)
    {
        if (Gen_subentries[i].DataType != SNDBNK_TYPE_VAG)
        {
            if (se_duplicateof[i] >= 0)
            {
                printf("DUPLICATE: [%d | %d/%d] %d is a duplicate of %d, linking and skipping write...\n", GenEntry.Index, i + 1, SubEntryCount, i, se_duplicateof[i]);
            }
            else
            {
                strcpy(AudioFilename, InFilename);
                FileExtPoint = strrchr(AudioFilename, '.');
                sprintf(FileExtPoint, "_%d.at3", i);
                sprintf(IniSectionGen, "%d", i);
                if (stat(AudioFilename, &st))
                {
                    printf("ERROR: Can't find %s during file writing!\n", AudioFilename);
                    return -1;
                }

                printf("WRITING: [%d | %d/%d] %s (@0x%X size: 0x%X)\n", GenEntry.Index, i + 1, SubEntryCount, AudioFilename, data_cursor, st.st_size + 4);

                InFile = fopen(AudioFilename, "rb");
                if (InFile == NULL)
                {
                    printf("ERROR: Error opening file for reading: %s\n", AudioFilename);
                    perror("ERROR");
                    return -1;
                }
                // read file to memory
                AudioFileBuffer = malloc(st.st_size);
                fread(AudioFileBuffer, st.st_size, 1, InFile);
                fclose(InFile);

                // write size and then the file after it
                fwrite(&st.st_size, sizeof(int32_t), 1, OutFile);
                fwrite(AudioFileBuffer, st.st_size, 1, OutFile);

                // free memory
                free(AudioFileBuffer);

                data_cursor = data_cursor + st.st_size + 4;
            }
        }
    }

    // write ADSRs
    for (int i = 0; i < adsr_count; i++)
    {
        fwrite(&Gen_ADSRs[i], sizeof(vagadsr), 1, OutFile);
        if (SHDSver != 0x24)
            fwrite(&Gen_ADSR_ext[i], sizeof(vagadsr_ext), 1, OutFile);
    }
    // write padding & EP
    fseek(OutFile, (FilePos + AlignedEnd) - sizeof(int32_t), SEEK_SET);
    fputc('E', OutFile);
    fputc('P', OutFile);
    fwrite(&GenEntry.Index, sizeof(int16_t), 1, OutFile);

    // free up memory (we need to cleanup because we're dealing with A LOT OF FILES, so we have no room for memory leaks!)
    if (vag_count)
    {
        free(Gen_ADSRs);
        free(Gen_ADSR_ext);
        free(Gen_vagoffsets);
    }

    free(Gen_subentries);

    return 0;
}


int ExtractSndDat(const char* InFileName, const char* OutFilePath)
{
    FILE* InFile = fopen(InFileName, "rb");
    if (InFile == NULL)
    {
        printf("ERROR: Error opening file for reading: %s\n", InFileName);
        perror("ERROR");
        return -1;
    }
    
    //FILE* OutFile;

   /* sprintf(DefFileName, "%s.ini", OutFilePath);
    FILE* DefFile = fopen(DefFileName, "wb");
    if (DefFile == NULL)
    {
        printf("ERROR: Error opening file for writing: %s\n", DefFileName);
        perror("ERROR");
        return -1;
    }*/

    unsigned int counter = 0;


    while (!feof(InFile))
    {
        // Read entry info
        fread(&Entry, sizeof(snddat_entry), 1, InFile);
        fseek(InFile, -(int)(sizeof(snddat_entry)), SEEK_CUR);
        EntryBuffer = malloc(Entry.EntrySize);
        // Load entire entry into memory
        fread(EntryBuffer, Entry.EntrySize, 1, InFile);

        ExtractSubEntries(OutFilePath);

        // cleanup & increase counter
        //fclose(OutFile);
        free(EntryBuffer);
        counter++;
    }
    fclose(InFile);
    return 0;
}

int PackSndDat(char* InFolder, char* OutFilename)
{
    GetDirectoryListing(InFolder);

    // we're using min-max logic to avoid having to sort stuff... besides we're only using numeric filenames
    int startpoint = FindMinIniNumber();
    int endpoint = FindMaxIniNumber();

    FILE* OutFile = fopen(OutFilename, "wb");
    if (OutFile == NULL)
     {
         printf("ERROR: Error opening file for writing: %s\n", OutFilename);
         perror("ERROR");
         return -1;
     }

    for (int i = startpoint; i <= endpoint; i++)
    {
        cur_idx = i;
        sprintf(WorkFileName, "%s%s%d.ini", InFolder, path_separator_str, i);

        if (bFileExists(WorkFileName))
        {
            printf("WRITING: %s\n", WorkFileName);
            PackEntry(WorkFileName, OutFilename, OutFile);
        }
    }
    fclose(OutFile);

    return 0;
}

long ScanELF(FILE* fin, long* outOffset)
{
    long size = 0;
    int32_t magic = 0;
    long oldoffset = ftell(fin);
    long endoffset = 0;

    while (!feof(fin))
    {
        fread(&magic, sizeof(int32_t), 1, fin);
        if (magic == 0x53444853)
        {
            *outOffset = ftell(fin) - 4;
            break;
        }
    }

    fseek(fin, oldoffset, SEEK_SET);


    while (!feof(fin))
    {
        fread(&magic, sizeof(int32_t), 1, fin);
        if (magic == 0x45444853)
        {
            endoffset = ftell(fin);
            break;
        }
    }

    fseek(fin, oldoffset, SEEK_SET);

    size = endoffset - (*outOffset);

    return size;
}

void LoadELFChunks(FILE* fin, int32_t filesize)
{
    long oldoffset = ftell(fin);
    ElfBottomSize = filesize - (ElfSHDSOffset + ElfSHDSSize);
    ElfChunkTop = malloc(ElfSHDSOffset);
    ElfChunkBottom = malloc(ElfBottomSize);
    SHDSbuffer = malloc(ElfSHDSSize);

    fread(ElfChunkTop, ElfSHDSOffset, 1, fin);
    fread(SHDSbuffer, ElfSHDSSize, 1, fin);
    fread(ElfChunkBottom, ElfBottomSize, 1, fin);
}

int LoadSHDS_ELF(char* InFilename)
{
    FILE* fin = fopen(InFilename, "rb");
    if (fin == NULL)
    {
        printf("ERROR: Error opening file for reading: %s\n", InFilename);
        perror("ERROR");
        return -1;
    }

    ElfSHDSSize = ScanELF(fin, &ElfSHDSOffset);
    SHDStotalsize = ElfSHDSSize;

    if (ElfSHDSSize)
    {
        struct stat fst = { 0 };
        stat(InFilename, &fst);
        LoadELFChunks(fin, fst.st_size);
        fclose(fin);

        // get values and pointers
        SHDSver = *(int16_t*)(((int)SHDSbuffer) + 0x8);
        PacCount = *(int16_t*)(((int)SHDSbuffer) + 0xC) + *(int16_t*)(((int)SHDSbuffer) + 0x10);

        int32_t pacoffsets = (*(int16_t*)(((int)SHDSbuffer) + 0xE)) & 0xFFFF;
        pacoffsets = pacoffsets + PacCount;

        if (SHDSver == 0x24)
        {
            pacoffsets = pacoffsets << 3;
            ADSREntrySize = ADSR_ENTRY_SIZE_TF1;
        }
        else
            pacoffsets = pacoffsets << 2;
        pacoffsets = pacoffsets + 0x34;

        SHDSsizes = (LoadPacAddr*)(pacoffsets + (int)SHDSbuffer);
        SHDSoffsets = (int32_t*)(((PacCount << 4) + pacoffsets) + (int)SHDSbuffer);

        if (bTF6mode)
            TF6voicepacoffset = SHDSoffsets[49];
    }
    else
        fclose(fin);

    return 0;
}

int LoadSHDS_Single(char* InFilename)
{
    FILE* fin = fopen(InFilename, "rb");
    if (fin == NULL)
    {
        printf("ERROR: Error opening file for reading: %s\n", InFilename);
        perror("ERROR");
        return -1;
    }

    struct stat fst = { 0 };
    stat(InFilename, &fst);
    SHDStotalsize = fst.st_size;

    SHDSbuffer = malloc(fst.st_size);
    fread(SHDSbuffer, fst.st_size, 1, fin);
    fclose(fin);

    // get values and pointers
    SHDSver = *(int16_t*)(((int)SHDSbuffer) + 0x8);
    PacCount = *(int16_t*)(((int)SHDSbuffer) + 0xC) + *(int16_t*)(((int)SHDSbuffer) + 0x10);

    int32_t pacoffsets = (*(int16_t*)(((int)SHDSbuffer) + 0xE)) & 0xFFFF;
    pacoffsets = pacoffsets + PacCount;

    if (SHDSver == 0x24)
    {
        pacoffsets = pacoffsets << 3;
        ADSREntrySize = ADSR_ENTRY_SIZE_TF1;
    }
    else
        pacoffsets = pacoffsets << 2;
    pacoffsets = pacoffsets + 0x34;

    SHDSsizes = (LoadPacAddr*)(pacoffsets + (int)SHDSbuffer);
    SHDSoffsets = (int32_t*)(((PacCount << 4) + pacoffsets) + (int)SHDSbuffer);

    if (bTF6mode)
        TF6voicepacoffset = SHDSoffsets[49];

    return 0;
}

int LoadSHDS(char* InFilename)
{
    FILE* fin = fopen(InFilename, "rb");
    int32_t magic = 0;
    if (fin == NULL)
    {
        printf("ERROR: Error opening file for reading: %s\n", InFilename);
        perror("ERROR");
        return -1;
    }

    fread(&magic, sizeof(int32_t), 1, fin);
    fclose(fin);

    switch (magic)
    {
    case 0x464C457F:
        bElfMode = true;
        printf("Loading SHDS from ELF\n");
        LoadSHDS_ELF(InFilename);
        break;
    case 0x53444853:
        printf("Loading SHDS from file\n");
        LoadSHDS_Single(InFilename);
        break;
    default:
        printf("WARNING: Unknown file passed for SHDS patching! Magic read: 0x%X\n", magic);
        break;
    }

    return 0;
}

int WriteSHDS_ELF(char* OutFilename)
{
    strcpy(OutPathSHDS, OutFilename);
    char* pp = strrchr(OutPathSHDS, '.');
    if (pp)
        *pp = 0;
    strcat(OutPathSHDS, "_EBOOT.BIN");

    printf("Writing SHDS to ELF: %s\n", OutPathSHDS);

    FILE* fout = fopen(OutPathSHDS, "wb");
    if (fout == NULL)
    {
        printf("ERROR: Error opening file for writing: %s\n", OutPathSHDS);
        perror("ERROR");
        return -1;
    }

    fwrite(ElfChunkTop, ElfSHDSOffset, 1, fout);
    fwrite(SHDSbuffer, ElfSHDSSize, 1, fout);
    fwrite(ElfChunkBottom, ElfBottomSize, 1, fout);
    fclose(fout);

    return 0;
}

int WriteSHDS(char* OutFilename)
{
    strcpy(OutPathSHDS, OutFilename);
    char* pp = strrchr(OutPathSHDS, '.');
    if (pp)
        *pp = 0;
    strcat(OutPathSHDS, "_SHDS.bin");

    printf("Writing SHDS to file: %s\n", OutPathSHDS);

    FILE* fout = fopen(OutPathSHDS, "wb");
    if (fout == NULL)
    {
        printf("ERROR: Error opening file for writing: %s\n", OutPathSHDS);
        perror("ERROR");
        return -1;
    }
    fwrite(SHDSbuffer, SHDStotalsize, 1, fout);
    fclose(fout);

    return 0;
}

int main(int argc, char *argv[])
{
    printf("Yu-Gi-Oh! Tag Force SNDDAT repacker\n");
    if (argc < 2)
    {
        printf("ERROR: Too few arguments.\nUSAGE (extraction): %s psp_snddat.bin [OutDir]\nUSAGE (extraction TF1): %s -1 psp_snddat.bin [OutDir]\nUSAGE (pack): %s -w InSHDS InDir [OutFilename]\nUSAGE (pack TF6): %s -w6 InSHDS InDir [OutFilename]\nUSAGE (pack single): %s -s InIniFile [OutFile]\nYou may also use the decrypted EBOOT file in place of the SHDS file.\n", argv[0], argv[0], argv[0], argv[0], argv[0]);
        return -1;
    }

    if (argv[1][0] == '-' && argv[1][1] == 'w')
    {
        if (argv[1][2] == '6')
            bTF6mode = true;

        if (argv[4] != NULL)
            strcpy(OutPath, argv[4]);
        else
        {
            strcpy(OutPath, argv[3]);
            strcat(OutPath, ".bin");
        }
        
        LoadSHDS(argv[2]);
        PackSndDat(argv[3], OutPath);

        if (bElfMode)
            WriteSHDS_ELF(OutPath);
        else
            WriteSHDS(OutPath);
        return 0;
    }
    // single write...
    if (argv[1][0] == '-' && argv[1][1] == 's')
    {
        if (argv[3] != NULL)
            strcpy(OutPath, argv[3]);
        else
        {
            strcpy(OutPath, argv[2]);
            strcat(OutPath, ".bin");
        }

        FILE* OutFile = fopen(OutPath, "wb");
        if (OutFile == NULL)
        {
            printf("ERROR: Error opening file for writing: %s\n", OutPath);
            perror("ERROR");
            return -1;
        }

        return PackEntry(argv[2], OutPath, OutFile);
    }
    if (argv[1][0] == '-' && argv[1][1] == '1')
    {
        bTF1mode = true;
        ADSREntrySize = ADSR_ENTRY_SIZE_TF1;
        if (argv[3] != NULL)
            strcpy(OutPath, argv[3]);
        else
        {
            char* autogen;
            strcpy(OutPath, argv[2]);
            autogen = strrchr(OutPath, '.');
            if (autogen)
                *autogen = 0;
        }

        printf("Creating directory: %s\n", OutPath);
        sprintf(MkDirString, "mkdir \"%s\"", OutPath);
        system(MkDirString);

        return ExtractSndDat(argv[2], OutPath);
    }

    if (argv[2] != NULL)
        strcpy(OutPath, argv[2]);
    else
    {
        char* autogen;
        strcpy(OutPath, argv[1]);
        autogen = strrchr(OutPath, '.');
        if (autogen)
            *autogen = 0;
    }

    ADSREntrySize = ADSR_ENTRY_SIZE;

    printf("Creating directory: %s\n", OutPath);
    sprintf(MkDirString, "mkdir \"%s\"", OutPath);
    system(MkDirString);

    return ExtractSndDat(argv[1], OutPath);
}
