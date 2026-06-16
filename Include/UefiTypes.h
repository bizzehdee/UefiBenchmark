#pragma once
// Minimal self-contained UEFI type definitions for x86-64.
// Provides only the types, protocols, and constants needed by this project.
// For production builds use the real EDK II headers via the .inf file.

extern "C" {

// ── Calling convention ────────────────────────────────────────
#if defined(__GNUC__) || defined(__clang__)
  #define EFIAPI __attribute__((ms_abi))
#elif defined(_MSC_VER)
  #define EFIAPI __cdecl
#else
  #define EFIAPI
#endif

#define IN
#define OUT
#define OPTIONAL
#define CONST const

// ── Fundamental types (exact width, x86-64) ──────────────────
typedef unsigned char       UINT8;
typedef unsigned short      UINT16;
typedef unsigned int        UINT32;
typedef unsigned long long  UINT64;
typedef signed char         INT8;
typedef signed short        INT16;
typedef signed int          INT32;
typedef signed long long    INT64;
// On a Linux host build UINTN must match size_t (unsigned long) so that
// operator new and mem* declarations satisfy the compiler's built-in checks.
// Both are 64-bit on x86-64, so there is no ABI difference.
#ifdef UEFI_HOST_TEST
typedef unsigned long       UINTN;
typedef long                INTN;
#else
typedef UINT64              UINTN;
typedef INT64               INTN;
#endif
typedef UINT8               BOOLEAN;
typedef unsigned short      CHAR16;
typedef char                CHAR8;
typedef void                VOID;

#define TRUE  1
#define FALSE 0
#define NULL  nullptr

// ── Binary byte-size units ───────────────────────────────────
static constexpr UINT64 BYTES_PER_KB = 1024ULL;
static constexpr UINT64 BYTES_PER_MB = 1024ULL * 1024ULL;
static constexpr UINT64 BYTES_PER_GB = 1024ULL * 1024ULL * 1024ULL;

// ── EFI_STATUS ───────────────────────────────────────────────
typedef UINTN EFI_STATUS;

#define EFI_SUCCESS              0ULL
#define EFI_ERROR_BIT            0x8000000000000000ULL
#define EFI_UNSUPPORTED          (EFI_ERROR_BIT | 3ULL)
#define EFI_NOT_READY            (EFI_ERROR_BIT | 6ULL)
#define EFI_BUFFER_TOO_SMALL     (EFI_ERROR_BIT | 5ULL)
#define EFI_INVALID_PARAMETER    (EFI_ERROR_BIT | 2ULL)

#define EFI_ERROR(Status)        (((INTN)(Status)) < 0)

// ── Handle / Event ───────────────────────────────────────────
typedef VOID* EFI_HANDLE;
typedef VOID* EFI_EVENT;

// ── GUID ─────────────────────────────────────────────────────
typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

// ── Memory ───────────────────────────────────────────────────
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32                Type;
    EFI_PHYSICAL_ADDRESS  PhysicalStart;
    EFI_VIRTUAL_ADDRESS   VirtualStart;
    UINT64                NumberOfPages;
    UINT64                Attribute;
} EFI_MEMORY_DESCRIPTOR;

#define EFI_PAGE_SIZE 4096

// ── Forward declarations ─────────────────────────────────────
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_BOOT_SERVICES;
struct EFI_RUNTIME_SERVICES;

// ── Input key ────────────────────────────────────────────────
typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

// Scan codes for special keys
#define SCAN_NULL   0x00
#define SCAN_UP     0x01
#define SCAN_DOWN   0x02
#define SCAN_RIGHT  0x03
#define SCAN_LEFT   0x04
#define SCAN_HOME   0x05
#define SCAN_END    0x06
#define SCAN_INSERT 0x08
#define SCAN_DELETE 0x09
#define SCAN_PAGE_UP   0x09
#define SCAN_PAGE_DOWN 0x0A
#define SCAN_ESC    0x17

// ── Simple Text Input Protocol ───────────────────────────────
typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET)(
    IN struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
    IN BOOLEAN ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(
    IN  struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
    OUT EFI_INPUT_KEY *Key
);

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_INPUT_RESET    Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    EFI_EVENT          WaitForKey;
};

// ── Simple Text Output Protocol ──────────────────────────────
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    IN struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN CHAR16 *String
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
    IN struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(
    IN struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN Attribute
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION)(
    IN struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN Column,
    IN UINTN Row
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_ENABLE_CURSOR)(
    IN struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN BOOLEAN Visible
);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID*                          Reset;
    EFI_TEXT_STRING                 OutputString;
    VOID*                          TestString;
    VOID*                          QueryMode;
    VOID*                          SetMode;
    EFI_TEXT_SET_ATTRIBUTE         SetAttribute;
    EFI_TEXT_CLEAR_SCREEN          ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION   SetCursorPosition;
    EFI_TEXT_ENABLE_CURSOR         EnableCursor;
    VOID*                          Mode;
};

// ── Graphics Output Protocol (GOP) ──────────────────────────
typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32                    Version;
    UINT32                    HorizontalResolution;
    UINT32                    VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK         PixelInformation;
    UINT32                    PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                                MaxMode;
    UINT32                                Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN                                 SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                  FrameBufferBase;
    UINTN                                 FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    IN  struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN  UINT32 ModeNumber,
    OUT UINTN *SizeOfInfo,
    OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info
);

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    IN struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN UINT32 ModeNumber
);

typedef struct {
    UINT8 Blue;
    UINT8 Green;
    UINT8 Red;
    UINT8 Reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

typedef enum {
    EfiBltVideoFill          = 0,
    EfiBltVideoToBltBuffer   = 1,
    EfiBltBufferToVideo      = 2,
    EfiBltVideoToVideo       = 3,
    EfiGraphicsOutputBltOperationMax
} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT)(
    IN struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer OPTIONAL,
    IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
    IN UINTN SourceX, IN UINTN SourceY,
    IN UINTN DestinationX, IN UINTN DestinationY,
    IN UINTN Width, IN UINTN Height,
    IN UINTN Delta OPTIONAL
);

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE   SetMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT        Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE*      Mode;
};

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

// ── Boot Services ────────────────────────────────────────────
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    IN  EFI_MEMORY_TYPE PoolType,
    IN  UINTN Size,
    OUT VOID **Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(IN VOID *Buffer);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    IN     EFI_ALLOCATE_TYPE Type,
    IN     EFI_MEMORY_TYPE MemoryType,
    IN     UINTN Pages,
    IN OUT EFI_PHYSICAL_ADDRESS *Memory
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(
    IN EFI_PHYSICAL_ADDRESS Memory,
    IN UINTN Pages
);

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    IN OUT UINTN *MemoryMapSize,
    IN OUT EFI_MEMORY_DESCRIPTOR *MemoryMap,
    OUT    UINTN *MapKey,
    OUT    UINTN *DescriptorSize,
    OUT    UINT32 *DescriptorVersion
);

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    IN  EFI_GUID *Protocol,
    IN  VOID *Registration OPTIONAL,
    OUT VOID **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_STALL)(IN UINTN Microseconds);

typedef EFI_STATUS (EFIAPI *EFI_SET_WATCHDOG_TIMER)(
    IN UINTN Timeout,
    IN UINT64 WatchdogCode,
    IN UINTN DataSize,
    IN CHAR16 *WatchdogData OPTIONAL
);

typedef EFI_STATUS (EFIAPI *EFI_WAIT_FOR_EVENT)(
    IN  UINTN NumberOfEvents,
    IN  EFI_EVENT *Event,
    OUT UINTN *Index
);

typedef VOID (EFIAPI *EFI_COPY_MEM)(IN VOID *Dest, IN VOID *Src, IN UINTN Length);
typedef VOID (EFIAPI *EFI_SET_MEM)(IN VOID *Buffer, IN UINTN Size, IN UINT8 Value);

// Table header shared by all UEFI tables
typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER       Hdr;

    // Task Priority Services (2 pointers)
    VOID*                  RaiseTPL;
    VOID*                  RestoreTPL;

    // Memory Services (5 pointers)
    EFI_ALLOCATE_PAGES     AllocatePages;
    EFI_FREE_PAGES         FreePages;
    EFI_GET_MEMORY_MAP     GetMemoryMap;
    EFI_ALLOCATE_POOL      AllocatePool;
    EFI_FREE_POOL          FreePool;

    // Event & Timer Services (6 pointers)
    VOID*                  CreateEvent;
    VOID*                  SetTimer;
    EFI_WAIT_FOR_EVENT     WaitForEvent;
    VOID*                  SignalEvent;
    VOID*                  CloseEvent;
    VOID*                  CheckEvent;

    // Protocol Handler Services (6 pointers)
    VOID*                  InstallProtocolInterface;
    VOID*                  ReinstallProtocolInterface;
    VOID*                  UninstallProtocolInterface;
    VOID*                  HandleProtocol;
    VOID*                  Reserved1;
    VOID*                  RegisterProtocolNotify;

    // Image Services (5 pointers)
    VOID*                  LocateHandle;
    VOID*                  LocateDevicePath;
    VOID*                  InstallConfigurationTable;
    VOID*                  LoadImage;
    VOID*                  StartImage;

    // Miscellaneous Services (3 pointers)
    VOID*                  Exit;
    VOID*                  UnloadImage;
    VOID*                  ExitBootServices;

    // Misc Services cont. (3 pointers)
    VOID*                  GetNextMonotonicCount;
    EFI_STALL              Stall;
    EFI_SET_WATCHDOG_TIMER SetWatchdogTimer;

    // DriverSupport Services (2 pointers)
    VOID*                  ConnectController;
    VOID*                  DisconnectController;

    // Open/Close Protocol (3 pointers)
    VOID*                  OpenProtocol;
    VOID*                  CloseProtocol;
    VOID*                  OpenProtocolInformation;

    // Library Services (3 pointers)
    VOID*                  ProtocolsPerHandle;
    VOID*                  LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL    LocateProtocol;

    // Remaining (3 pointers)
    VOID*                  InstallMultipleProtocolInterfaces;
    VOID*                  UninstallMultipleProtocolInterfaces;
    VOID*                  CalculateCrc32;

    EFI_COPY_MEM           CopyMem;
    EFI_SET_MEM            SetMem;

    VOID*                  CreateEventEx;
};

// ── Runtime Services (stub — not used, but needed for table layout) ──
struct EFI_RUNTIME_SERVICES {
    EFI_TABLE_HEADER Hdr;
    VOID* GetTime;
    VOID* SetTime;
    VOID* GetWakeupTime;
    VOID* SetWakeupTime;
    VOID* SetVirtualAddressMap;
    VOID* ConvertPointer;
    VOID* GetVariable;
    VOID* GetNextVariableName;
    VOID* SetVariable;
    VOID* GetNextHighMonotonicCount;
    VOID* ResetSystem;
    VOID* UpdateCapsule;
    VOID* QueryCapsuleCapabilities;
    VOID* QueryVariableInfo;
};

// ── System Table ─────────────────────────────────────────────
typedef struct {
    EFI_TABLE_HEADER                    Hdr;
    CHAR16*                             FirmwareVendor;
    UINT32                              FirmwareRevision;
    EFI_HANDLE                          ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL*     ConIn;
    EFI_HANDLE                          ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*    ConOut;
    EFI_HANDLE                          StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*    StdErr;
    EFI_RUNTIME_SERVICES*              RuntimeServices;
    EFI_BOOT_SERVICES*                 BootServices;
    UINTN                              NumberOfTableEntries;
    VOID*                              ConfigurationTable;
} EFI_SYSTEM_TABLE;

// ── MP Services Protocol (PI Spec — multi-core dispatch) ─────
typedef VOID (EFIAPI *EFI_AP_PROCEDURE)(IN VOID *Buffer);

// Processor location from GetProcessorInfo
typedef struct {
    UINT32 Package;
    UINT32 Core;
    UINT32 Thread;
} EFI_CPU_PHYSICAL_LOCATION;

typedef struct {
    UINT64                  ProcessorId;  // APIC ID
    UINT32                  StatusFlag;   // bit0=BSP, bit1=enabled, bit2=healthy
    EFI_CPU_PHYSICAL_LOCATION Location;
} EFI_PROCESSOR_INFORMATION;

#define PROCESSOR_AS_BSP_BIT        0x00000001u
#define PROCESSOR_ENABLED_BIT       0x00000002u
#define PROCESSOR_HEALTH_STATUS_BIT 0x00000004u

struct EFI_MP_SERVICES_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_MP_SERVICES_GET_NUMBER_OF_PROCESSORS)(
    IN  struct EFI_MP_SERVICES_PROTOCOL *This,
    OUT UINTN *NumberOfProcessors,
    OUT UINTN *NumberOfEnabledProcessors
);

typedef EFI_STATUS (EFIAPI *EFI_MP_SERVICES_GET_PROCESSOR_INFO)(
    IN  struct EFI_MP_SERVICES_PROTOCOL *This,
    IN  UINTN ProcessorNumber,
    OUT EFI_PROCESSOR_INFORMATION *ProcessorInfoBuffer
);

typedef EFI_STATUS (EFIAPI *EFI_MP_SERVICES_STARTUP_ALL_APS)(
    IN  struct EFI_MP_SERVICES_PROTOCOL *This,
    IN  EFI_AP_PROCEDURE Procedure,
    IN  BOOLEAN SingleThread,
    IN  EFI_EVENT WaitEvent OPTIONAL,
    IN  UINTN TimeoutInMicroseconds,
    IN  VOID *ProcedureArgument OPTIONAL,
    OUT UINTN **FailedCpuList OPTIONAL
);

typedef EFI_STATUS (EFIAPI *EFI_MP_SERVICES_STARTUP_THIS_AP)(
    IN  struct EFI_MP_SERVICES_PROTOCOL *This,
    IN  EFI_AP_PROCEDURE Procedure,
    IN  UINTN ProcessorNumber,
    IN  EFI_EVENT WaitEvent OPTIONAL,
    IN  UINTN TimeoutInMicroseconds,
    IN  VOID *ProcedureArgument OPTIONAL,
    OUT BOOLEAN *Finished OPTIONAL
);

typedef EFI_STATUS (EFIAPI *EFI_MP_SERVICES_ENABLE_DISABLE_AP)(
    IN  struct EFI_MP_SERVICES_PROTOCOL *This,
    IN  UINTN ProcessorNumber,
    IN  BOOLEAN EnableAP,
    IN  UINT32 *HealthFlag OPTIONAL
);

typedef EFI_STATUS (EFIAPI *EFI_MP_SERVICES_WHO_AM_I)(
    IN  struct EFI_MP_SERVICES_PROTOCOL *This,
    OUT UINTN *ProcessorNumber
);

struct EFI_MP_SERVICES_PROTOCOL {
    EFI_MP_SERVICES_GET_NUMBER_OF_PROCESSORS GetNumberOfProcessors;
    EFI_MP_SERVICES_GET_PROCESSOR_INFO       GetProcessorInfo;
    EFI_MP_SERVICES_STARTUP_ALL_APS          StartupAllAPs;
    EFI_MP_SERVICES_STARTUP_THIS_AP          StartupThisAP;
    VOID*                                     SwitchBSP;
    EFI_MP_SERVICES_ENABLE_DISABLE_AP        EnableDisableAP;
    EFI_MP_SERVICES_WHO_AM_I                 WhoAmI;
};

#define EFI_MP_SERVICES_PROTOCOL_GUID \
    { 0x3fdda605, 0xa76e, 0x4f46, \
      { 0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3d, 0x08 } }

// ── Configuration Table (for SMBIOS and other table lookup) ──
typedef struct {
    EFI_GUID VendorGuid;
    VOID*    VendorTable;
} EFI_CONFIGURATION_TABLE;

// SMBIOS 2.x table GUID
#define SMBIOS_TABLE_GUID \
    { 0xeb9d2d31, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

// SMBIOS 3.x table GUID (64-bit entry point)
#define SMBIOS3_TABLE_GUID \
    { 0xf2fd1544, 0x9794, 0x4a2c, { 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 } }

// ── Global accessors (set once in EfiMain) ───────────────────
extern EFI_SYSTEM_TABLE*   gST;
extern EFI_BOOT_SERVICES*  gBS;
extern EFI_HANDLE          gImageHandle;

} // extern "C"
