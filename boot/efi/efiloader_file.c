
EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {0x964E5B22, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

EFI_STATUS ReadFile(struct EFI_SYSTEM_TABLE* st, EFI_HANDLE ImageHandle, uint16_t* Path, void** BufferOut, uintN_t* SizeOut) {
    struct EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
    EFI_STATUS Status = st->BootServices->OpenProtocol(ImageHandle, &EFI_LOADED_IMAGE_PROTOCOL_GUID, (void**)&LoadedImage, ImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to get LoadedImage protocol\r\n");
        return Status;
    }

    struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
    Status = st->BootServices->OpenProtocol(LoadedImage->DeviceHandle, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, (void**)&FileSystem, ImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to get FileSystem protocol\r\n");
        return Status;
    }

    struct EFI_FILE_PROTOCOL* Root;
    Status = FileSystem->OpenVolume(FileSystem, &Root);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to open volume\r\n");
        return Status;
    }

    struct EFI_FILE_PROTOCOL* File;
    Status = Root->Open(Root, &File, Path, EFI_FILE_MODE_READ, 0);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to open file\r\n");
        return Status;
    }

    // Get file size
    EFI_GUID FileInfoGuid = {0x09576E92, 0x6D3F, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
    char InfoBuffer[256];
    uintN_t InfoSize = sizeof(InfoBuffer);
    Status = File->GetInfo(File, &FileInfoGuid, &InfoSize, InfoBuffer);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to get file info\r\n");
        return Status;
    }

    // EFI_FILE_INFO struct: FileSize is at offset 8 (uint64_t)
    uint64_t FileSize = *(uint64_t*)(InfoBuffer + 8);

    void* FileBuffer;
    Status = st->BootServices->AllocatePool(EfiLoaderData, FileSize, &FileBuffer);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to allocate memory for file\r\n");
        return Status;
    }

    uintN_t ReadSize = FileSize;
    Status = File->Read(File, &ReadSize, FileBuffer);
    if (Status != EFI_SUCCESS || ReadSize != FileSize) {
        Print(st, u"Failed to read file\r\n");
        return Status;
    }

    File->Close(File);
    Root->Close(Root);

    *BufferOut = FileBuffer;
    *SizeOut = FileSize;
    return EFI_SUCCESS;
}
