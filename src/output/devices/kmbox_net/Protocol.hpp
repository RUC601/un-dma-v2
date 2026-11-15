#pragma once
#include "output/OutputTypes.hpp"

// Command definitions for KMbox NET communication
#define cmd_connect         0xaf3c2828
#define cmd_mouse_move      0xaede7345
#define cmd_mouse_left      0x9823AE8D
#define cmd_mouse_middle    0x97a3AE8D
#define cmd_mouse_right     0x238d8212
#define cmd_mouse_wheel     0xffeead38
#define cmd_mouse_automove  0xaede7346
#define cmd_keyboard_all    0x123c2c2f
#define cmd_reboot          0xaa8855aa
#define cmd_bazerMove       0xa238455a
#define cmd_monitor         0x27388020
#define cmd_debug           0x27382021
#define cmd_mask_mouse      0x23234343
#define cmd_unmask_all      0x23344343
#define cmd_setconfig       0x1d3d3323
#define cmd_setvidpid       0xffed3232
#define cmd_showpic         0x12334883


typedef struct
{
    unsigned int mac;        // Device MAC address
    unsigned int rand;       // Random value
    unsigned int indexpts;   // Timestamp / sequence index
    unsigned int cmd;        // Command opcode
} cmd_head_t;

typedef struct
{
    unsigned char buff[1024];
} cmd_data_t;

typedef struct
{
    unsigned short buff[512];
} cmd_u16_t;

// Software-defined mouse data
typedef struct
{
    int button;
    int x;
    int y;
    int wheel;
    int point[10];
} soft_mouse_t;

// Software-defined keyboard data
typedef struct
{
    char ctrl;
    char resvel;
    char button[10];
} soft_keyboard_t;

// Client data packet (header + payload union)
typedef struct
{
    cmd_head_t head;
    union {
        cmd_data_t      u8buff;       // Raw byte buffer
        cmd_u16_t       u16buff;      // U16 buffer
        soft_mouse_t    cmd_mouse;    // Mouse command data
        soft_keyboard_t cmd_keyboard; // Keyboard command data
    };
} client_data;

struct OutputQueuedNetCommand {
    client_data data{};
    int length = 0;
    OutputCommandType type = OutputCommandType::Unknown;
    OutputIntent outputIntent = OutputIntent::Normal;
    OutputCommandPriority priority = OutputCommandPriority::Normal;
    std::shared_ptr<OutputCommandCompletion> completion;
    int mouseButtonStateMask = -1;
    std::chrono::steady_clock::time_point enqueuedAt{};
};


#pragma pack(1)
typedef struct {
    unsigned char report_id;
    unsigned char buttons;
    short x;
    short y;
    short wheel;
} standard_mouse_report_t;

typedef struct {
    unsigned char report_id;
    unsigned char buttons;
    unsigned char data[10];
} standard_keyboard_report_t;
#pragma pack()

