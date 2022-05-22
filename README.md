# TFSndEdit

Yu-Gi-Oh! Tag Force SNDDAT repacker

This is an evolution of the [Yu-Gi-Oh! Tag Force Music Extractor](https://github.com/xan1242/TagForceMusicEx) I have made previously.
You can extract and repack the sound banks hassle-free using this utility.

This was tested with Tag Force 1 and Tag Force 6 with full success.

## Usage

Extraction: `TFSndEdit psp_snddat.bin [OutFolder]`

Repacking: `TFSndEdit -w SHDS_file snddat_folder [OutFile.bin]`

Repacking (TF6 mode): `TFSndEdit -w6 SHDS_file snddat_folder [OutFile.bin]`

Pack single entry: `TFSndEdit -s snddat_folder/X.ini [OutFile.bin]` (where X is the index of the entry)

You *need* to provide the SHDS (sound header) file extracted from the executable (EBOOT.BIN). This is required to properly change the offsets and sizes accordingly. It should be easy to find as the file begins with `SHDS` and ends with `SHDE`.

Keep in mind that you MUST keep the filenames in the same format as extracted because the utility highly depends on it for file reference and indexing. There are thousands of files, so high speed storage is desirable (SSD at least).

The utility will extract the files as they're found in the banks (including duplicates). Duplicates are auto-detected during repacking process so they won't be packed back again (unless you change the file).

This utility can add sounds too, but you must provide a valid sound header (SHDS) file first.

## TODO

- identify unknown variables

- VAG header generation (currently only playable through PSound)

- CMake builds

- Generate SHDS from scratch if at all possible
