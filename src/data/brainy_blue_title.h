// "Pokemon Brainy Blue" animated title screen (local build only).
// Frames are 240x160 8bpp bitmaps for BG mode 4, sharing one 256-color palette.

static const u16 sBrainyBlueTitlePal[] = INCBIN_U16("graphics/title_screen/brainy_blue/palette.gbapal");

static const u32 sBrainyBlueFrame00[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_00.bin.lz");
static const u32 sBrainyBlueFrame01[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_01.bin.lz");
static const u32 sBrainyBlueFrame02[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_02.bin.lz");
static const u32 sBrainyBlueFrame03[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_03.bin.lz");
static const u32 sBrainyBlueFrame04[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_04.bin.lz");
static const u32 sBrainyBlueFrame05[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_05.bin.lz");
static const u32 sBrainyBlueFrame06[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_06.bin.lz");
static const u32 sBrainyBlueFrame07[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_07.bin.lz");
static const u32 sBrainyBlueFrame08[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_08.bin.lz");
static const u32 sBrainyBlueFrame09[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_09.bin.lz");
static const u32 sBrainyBlueFrame10[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_10.bin.lz");
static const u32 sBrainyBlueFrame11[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_11.bin.lz");
static const u32 sBrainyBlueFrame12[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_12.bin.lz");
static const u32 sBrainyBlueFrame13[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_13.bin.lz");
static const u32 sBrainyBlueFrame14[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_14.bin.lz");
static const u32 sBrainyBlueFrame15[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_15.bin.lz");
static const u32 sBrainyBlueFrame16[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_16.bin.lz");
static const u32 sBrainyBlueFrame17[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_17.bin.lz");
static const u32 sBrainyBlueFrame18[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_18.bin.lz");
static const u32 sBrainyBlueFrame19[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_19.bin.lz");
static const u32 sBrainyBlueFrame20[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_20.bin.lz");
static const u32 sBrainyBlueFrame21[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_21.bin.lz");
static const u32 sBrainyBlueFrame22[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_22.bin.lz");
static const u32 sBrainyBlueFrame23[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_23.bin.lz");
static const u32 sBrainyBlueFrame24[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_24.bin.lz");
static const u32 sBrainyBlueFrame25[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_25.bin.lz");
static const u32 sBrainyBlueFrame26[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_26.bin.lz");
static const u32 sBrainyBlueFrame27[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_27.bin.lz");
static const u32 sBrainyBlueFrame28[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_28.bin.lz");
static const u32 sBrainyBlueFrame29[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_29.bin.lz");
static const u32 sBrainyBlueFrame30[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_30.bin.lz");
static const u32 sBrainyBlueFrame31[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_31.bin.lz");
static const u32 sBrainyBlueFrame32[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_32.bin.lz");
static const u32 sBrainyBlueFrame33[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_33.bin.lz");
static const u32 sBrainyBlueFrame34[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_34.bin.lz");
static const u32 sBrainyBlueFrame35[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_35.bin.lz");
static const u32 sBrainyBlueFrame36[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_36.bin.lz");
static const u32 sBrainyBlueFrame37[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_37.bin.lz");
static const u32 sBrainyBlueFrame38[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_38.bin.lz");
static const u32 sBrainyBlueFrame39[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_39.bin.lz");
static const u32 sBrainyBlueFrame40[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_40.bin.lz");
static const u32 sBrainyBlueFrame41[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_41.bin.lz");
static const u32 sBrainyBlueFrame42[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_42.bin.lz");
static const u32 sBrainyBlueFrame43[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_43.bin.lz");
static const u32 sBrainyBlueFrame44[] = INCBIN_U32("graphics/title_screen/brainy_blue/frame_44.bin.lz");

static const u32 *const sBrainyBlueFrames[] =
{
    sBrainyBlueFrame00,
    sBrainyBlueFrame01,
    sBrainyBlueFrame02,
    sBrainyBlueFrame03,
    sBrainyBlueFrame04,
    sBrainyBlueFrame05,
    sBrainyBlueFrame06,
    sBrainyBlueFrame07,
    sBrainyBlueFrame08,
    sBrainyBlueFrame09,
    sBrainyBlueFrame10,
    sBrainyBlueFrame11,
    sBrainyBlueFrame12,
    sBrainyBlueFrame13,
    sBrainyBlueFrame14,
    sBrainyBlueFrame15,
    sBrainyBlueFrame16,
    sBrainyBlueFrame17,
    sBrainyBlueFrame18,
    sBrainyBlueFrame19,
    sBrainyBlueFrame20,
    sBrainyBlueFrame21,
    sBrainyBlueFrame22,
    sBrainyBlueFrame23,
    sBrainyBlueFrame24,
    sBrainyBlueFrame25,
    sBrainyBlueFrame26,
    sBrainyBlueFrame27,
    sBrainyBlueFrame28,
    sBrainyBlueFrame29,
    sBrainyBlueFrame30,
    sBrainyBlueFrame31,
    sBrainyBlueFrame32,
    sBrainyBlueFrame33,
    sBrainyBlueFrame34,
    sBrainyBlueFrame35,
    sBrainyBlueFrame36,
    sBrainyBlueFrame37,
    sBrainyBlueFrame38,
    sBrainyBlueFrame39,
    sBrainyBlueFrame40,
    sBrainyBlueFrame41,
    sBrainyBlueFrame42,
    sBrainyBlueFrame43,
    sBrainyBlueFrame44,
};

// How long each frame stays on screen, in 1/60 s (from the GIF's frame delays).
static const u8 sBrainyBlueFrameDurations[] =
{
    6, 6, 12, 12, 12, 6, 6, 6, 6, 6, 6, 6, 12, 6, 12, 12, 6, 6, 6, 6, 12, 6, 6, 6, 12, 6, 6, 6, 12, 6, 6, 6, 6, 12, 6, 6, 6, 6, 12, 6, 6, 6, 12, 6, 6,
};
