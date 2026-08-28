cat << 'END' > tmp_gop.c
typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    void* Info;
    uintN_t SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    uintN_t FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (*QueryMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL* This, uint32_t ModeNumber, uintN_t* SizeOfInfo, void** Info);
    EFI_STATUS (*SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL* This, uint32_t ModeNumber);
    void* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};
END
