#include "win/KeyboardDevices.h"

#include "core/DeviceMatch.h"

#include <cfgmgr32.h>
#include <dbt.h>

#include <cwchar>
#include <cwctype>
#include <map>

namespace km {
namespace {

// GUID_DEVINTERFACE_KEYBOARD and the device properties used for names.
const GUID kKeyboardInterface = {0x884b96c3, 0x56ef, 0x11d1, {0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd}};
const DEVPROPKEY kInstanceId = {{0x78c34fc8, 0x104a, 0x4aca, {0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57}}, 256};
const DEVPROPKEY kBusReportedDesc = {{0x540b947e, 0x8b40, 0x45bc, {0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2}}, 4};
const DEVPROPKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
const DEVPROPKEY kDeviceDesc = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 2};

std::vector<std::wstring> KeyboardInterfacePaths() {
    for (int attempt = 0; attempt < 4; ++attempt) {
        ULONG len = 0;
        if (CM_Get_Device_Interface_List_SizeW(&len, const_cast<GUID*>(&kKeyboardInterface), nullptr,
                                               CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
            return {};
        std::wstring buf(len, L'\0');
        CONFIGRET cr = CM_Get_Device_Interface_ListW(const_cast<GUID*>(&kKeyboardInterface), nullptr, buf.data(), len,
                                                     CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (cr == CR_BUFFER_SMALL) continue;  // A device arrived meanwhile.
        if (cr != CR_SUCCESS) return {};
        std::vector<std::wstring> paths;
        for (const wchar_t* p = buf.c_str(); *p; p += wcslen(p) + 1) paths.emplace_back(p);
        return paths;
    }
    return {};
}

std::wstring NodeString(DEVINST node, const DEVPROPKEY& key) {
    DEVPROPTYPE type = 0;
    wchar_t buf[256] = {};
    ULONG size = sizeof buf - sizeof(wchar_t);
    if (CM_Get_DevNode_PropertyW(node, &key, &type, reinterpret_cast<PBYTE>(buf), &size, 0) != CR_SUCCESS ||
        type != DEVPROP_TYPE_STRING)
        return {};
    return buf;
}

// Class-driver descriptions that say nothing about which keyboard it is.
bool IsGenericName(const std::wstring& name) {
    std::wstring n = name;
    for (wchar_t& c : n) c = static_cast<wchar_t>(std::towlower(c));
    for (const wchar_t* marker :
         {L"hid keyboard", L"hid-compliant", L"hid device", L"usb input device", L"composite", L"hub",
          L"host controller", L"generic", L"gatt", L"enumerator", L"input configuration", L"standard ps/2",
          L"bluetooth hid", L"keyboard device", L"converted"})
        if (n.find(marker) != std::wstring::npos) return true;
    return n.empty();
}

// Walks from the keyboard's HID node towards the physical device (USB
// device, Bluetooth device) and takes the first descriptive name.
std::wstring FriendlyName(const std::wstring& path, const std::string& id) {
    if (id.rfind("ACPI\\", 0) == 0) return DefaultKeyboardName(id);
    wchar_t instance[MAX_DEVICE_ID_LEN] = {};
    ULONG size = sizeof instance;
    DEVPROPTYPE type = 0;
    DEVINST node = 0;
    if (CM_Get_Device_Interface_PropertyW(path.c_str(), &kInstanceId, &type, reinterpret_cast<PBYTE>(instance), &size,
                                          0) != CR_SUCCESS ||
        CM_Locate_DevNodeW(&node, instance, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return DefaultKeyboardName(id);
    for (int depth = 0; depth < 4; ++depth) {
        for (const DEVPROPKEY* key : {&kBusReportedDesc, &kFriendlyName, &kDeviceDesc}) {
            if (key == &kDeviceDesc && depth > 1) continue;  // Deeper descriptions name controllers.
            std::wstring name = NodeString(node, *key);
            if (!IsGenericName(name)) return name;
        }
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
        node = parent;
    }
    return DefaultKeyboardName(id);
}

}  // namespace

std::vector<KeyboardDevice> EnumerateKeyboards() {
    std::vector<KeyboardDevice> out;
    std::map<std::string, size_t> index;
    for (const std::wstring& path : KeyboardInterfacePaths()) {
        const std::string id = KeyboardIdFromInterfacePath(path);
        if (id.empty()) continue;
        auto it = index.find(id);
        if (it == index.end()) {
            index[id] = out.size();
            out.push_back({id, FriendlyName(path, id)});
        } else if (out[it->second].name == DefaultKeyboardName(id)) {
            // Another interface of the same keyboard may know a better name.
            out[it->second].name = FriendlyName(path, id);
        }
    }
    return out;
}

HDEVNOTIFY WatchKeyboards(HWND hwnd) {
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof filter;
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = kKeyboardInterface;
    return RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
}

std::string KeyboardIdForRawDevice(HANDLE device) {
    if (!device) return {};
    UINT chars = 0;
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &chars) != 0 || chars == 0) return {};
    std::wstring name(chars, L'\0');
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &chars) == static_cast<UINT>(-1)) return {};
    name.resize(wcslen(name.c_str()));
    return KeyboardIdFromInterfacePath(name);
}

}  // namespace km
