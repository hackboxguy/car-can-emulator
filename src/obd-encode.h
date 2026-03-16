#ifndef OBD_ENCODE_H
#define OBD_ENCODE_H

#include <cstring>

// OBD2 response frame data (8 bytes, matching CAN frame data layout)
struct OBDResponse {
    unsigned char data[8];
    bool respond; // false = unsupported PID, do not send
};

// Encode an OBD2 Mode 01 response for the given PID and value.
// Returns a filled OBDResponse with respond=true, or respond=false for unsupported PIDs.
// The caller provides the raw human-readable value (e.g., rpm=3000, temp=90).
static inline OBDResponse encode_obd_response(unsigned char pid, int value)
{
    OBDResponse r;
    std::memset(r.data, 0, sizeof(r.data));
    r.respond = true;
    r.data[1] = 0x41; // Mode 01 response
    r.data[2] = pid;

    switch (pid)
    {
        case 0x04: // Engine load: 1 byte, percentage = value * 100 / 255
        {
            unsigned char enc = (unsigned char)(value * 255 / 100);
            r.data[0] = 0x03; r.data[3] = enc;
            break;
        }
        case 0x05: // Coolant temp: 1 byte, value = temp_c + 40
        {
            unsigned char enc = (unsigned char)(value + 40);
            r.data[0] = 0x03; r.data[3] = enc;
            break;
        }
        case 0x0B: // Intake pressure: 1 byte, direct kPa
        {
            r.data[0] = 0x03; r.data[3] = (unsigned char)value;
            break;
        }
        case 0x0C: // Engine RPM: 2 bytes BE, value = rpm * 4
        {
            unsigned short enc = (unsigned short)(value * 4);
            r.data[0] = 0x04; r.data[3] = (enc >> 8); r.data[4] = enc & 0xFF;
            break;
        }
        case 0x0D: // Vehicle speed: 1 byte, direct km/h
        {
            r.data[0] = 0x03; r.data[3] = (unsigned char)value;
            break;
        }
        case 0x10: // MAF air flow: 2 bytes BE
        {
            unsigned short flow = (unsigned short)value;
            r.data[0] = 0x04; r.data[3] = (flow >> 8); r.data[4] = flow & 0xFF;
            break;
        }
        case 0x2F: // Fuel tank level: 1 byte, percentage = value * 100 / 255
        {
            unsigned char enc = (unsigned char)(value * 255 / 100);
            r.data[0] = 0x03; r.data[3] = enc;
            break;
        }
        case 0x42: // Control module voltage: 2 bytes BE, millivolts
        {
            unsigned short mv = (unsigned short)value;
            r.data[0] = 0x04; r.data[3] = (mv >> 8); r.data[4] = mv & 0xFF;
            break;
        }
        case 0x00: // Supported PIDs 01-20
        {
            r.data[0] = 0x06; r.data[3] = 0x18; r.data[4] = 0x39; r.data[5] = 0x00; r.data[6] = 0x01;
            break;
        }
        case 0x20: // Supported PIDs 21-40
        {
            r.data[0] = 0x06; r.data[3] = 0x00; r.data[4] = 0x02; r.data[5] = 0x00; r.data[6] = 0x01;
            break;
        }
        case 0x40: // Supported PIDs 41-60
        {
            r.data[0] = 0x06; r.data[3] = 0x40;
            break;
        }
        default:
            r.respond = false;
            break;
    }
    return r;
}

#endif // OBD_ENCODE_H
