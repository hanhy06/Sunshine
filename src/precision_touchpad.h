/**
 * @file src/precision_touchpad.h
 * @brief Versioned remote precision touchpad frames and Windows injection device.
 */
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#ifdef _WIN32
  #include <windows.h>
#endif

namespace precision_touchpad {
  constexpr std::uint32_t magic = 0x55000008;  ///< Private input protocol message.
  constexpr std::uint32_t feature = 0x04;  ///< SDP capability for version 1 frames.
  constexpr std::size_t packet_size = 88;  ///< Fixed size including the standard input header.

  /**

   * @brief One contact in device-relative hundredths of a millimeter.

   */
  struct contact {
    std::uint32_t id, x, y;  ///< Stable contact ID and position.
  };

  /**

   * @brief A complete active-contact snapshot, or a device reset command.

   */
  struct frame {
    std::uint8_t command {}, count {};  ///< 1 = snapshot, 2 = cancel/destroy.
    std::uint32_t sequence {}, time {}, width {}, height {};  ///< Ordering, milliseconds, physical bounds.
    std::array<contact, 5> contacts {};  ///< Active contacts only; missing IDs are released.
  };

  /**
   * @brief Decode and validate a complete wire packet before touching the operating system.
   * @param bytes Complete input packet, including its header.
   * @param out Decoded snapshot; use only when this function succeeds.
   * @return Whether the packet is well-formed and within the device limits.
   */
  inline bool decode(std::span<const std::uint8_t> bytes, frame &out) {
    if (bytes.size() != packet_size || bytes[8] != 1 || bytes[11] != 0) {
      return false;
    }
    auto le32 = [&](std::size_t offset) {
      return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
             (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    };
    if (bytes[0] || bytes[1] || bytes[2] || bytes[3] != packet_size - 4 || le32(4) != magic) {
      return false;
    }
    out = {};
    out.command = bytes[9];
    out.count = bytes[10];
    out.sequence = le32(12);
    out.time = le32(16);
    out.width = le32(20);
    out.height = le32(24);
    if (out.command == 2) {
      return out.count == 0;
    }
    if (out.command != 1 || out.count > 5 || out.width < 100 || out.width > 100000 || out.height < 100 || out.height > 100000) {
      return false;
    }
    for (unsigned i = 0; i < out.count; ++i) {
      out.contacts[i] = {le32(28 + i * 12), le32(32 + i * 12), le32(36 + i * 12)};
      if (out.contacts[i].id > 31 || out.contacts[i].x > out.width || out.contacts[i].y > out.height) {
        return false;
      }
      for (unsigned j = 0; j < i; ++j) {
        if (out.contacts[i].id == out.contacts[j].id) {
          return false;
        }
      }
    }
    return true;
  }

#ifdef _WIN32
  /**
   * @brief Own a generic synthetic precision touchpad for one streaming session.
   */
  class device {
    /**
     * @brief ABI of CreateSyntheticPointerDevice2, also usable with older SDK headers.
     */
    struct creation_params {
      POINTER_INPUT_TYPE type;  ///< PT_TOUCHPAD (5).
      ULONG count;  ///< Maximum simultaneous contacts.
      POINTER_FEEDBACK_MODE feedback;  ///< No visual feedback.
      HMONITOR monitor;  ///< Null for indirect input.
      ULONG width, height, options;  ///< HIMETRIC size and SDCO_PHYSICAL_SIZE.
    };

    using create_fn = HSYNTHETICPOINTERDEVICE(WINAPI *)(const creation_params *);  ///< Runtime-loaded API.
    HSYNTHETICPOINTERDEVICE handle = nullptr;  ///< Owned OS device.
    frame previous {};  ///< Last successfully injected contact snapshot.
    bool ordered = false;  ///< Whether a sequence number has been accepted.
    std::uint32_t sequence = 0;  ///< Last accepted sequence number.
    std::uint32_t last_time = 0;  ///< Strictly increasing injected timestamp.

  public:
    device() = default;
    device(const device &) = delete;
    device &operator=(const device &) = delete;

    /**

     * @brief Release all contacts and dispose of the OS device.

     */
    ~device() {
      reset();
    }

    /**

     * @brief Destroy the device, which also cancels outstanding contacts.

     */
    void reset() {
      if (handle) {
        DestroySyntheticPointerDevice(handle);
      }
      handle = nullptr;
      previous = {};
      ordered = false;
      last_time = 0;
    }

    /**
     * @brief Create an unrestricted precision touchpad; report the Windows error on failure.
     * @param width Device width in hundredths of a millimeter.
     * @param height Device height in hundredths of a millimeter.
     * @return Whether Windows created the device. Failure sets GetLastError().
     */
    bool create(std::uint32_t width, std::uint32_t height) {
      reset();
      auto module = GetModuleHandleW(L"user32.dll");
      auto create_device = reinterpret_cast<create_fn>(GetProcAddress(module, "CreateSyntheticPointerDevice2"));
      if (!create_device) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
      }
      creation_params params {static_cast<POINTER_INPUT_TYPE>(5), 5, POINTER_FEEDBACK_NONE, nullptr, width, height, 1};
      handle = create_device(&params);
      if (!handle) {
        return false;
      }
      previous.width = width;
      previous.height = height;
      return true;
    }

    /**

     * @brief Test actual device creation before advertising the protocol capability.

     */
    static bool supported() {
      device probe;
      return probe.create(12000, 7500);
    }

    /**
     * @brief Inject a complete snapshot, including releases for disappeared contacts.
     * @param next Validated frame from decode().
     * @return Whether injection succeeded or a stale frame was ignored. Failure sets GetLastError().
     */
    bool submit(const frame &next) {
      if (ordered && static_cast<std::int32_t>(next.sequence - sequence) <= 0) {
        return true;
      }
      if (next.command == 2) {
        reset();
        return true;
      }
      if (!handle || next.width != previous.width || next.height != previous.height) {
        if (!create(next.width, next.height)) {
          return false;
        }
      }
      // Released contacts are sent first, avoiding a frame with more than five contacts
      // if a full set of fingers is replaced between two snapshots.
      std::array<POINTER_TYPE_INFO, 5> infos {};
      auto timestamp = next.time ? next.time : 1;
      if (last_time && static_cast<std::int32_t>(timestamp - last_time) <= 0) {
        timestamp = last_time + 1;
      }
      auto make_info = [&](contact c, bool active) {
        POINTER_TYPE_INFO info {};
        info.type = static_cast<POINTER_INPUT_TYPE>(5);
        auto &p = info.touchInfo.pointerInfo;
        p.pointerType = info.type;
        p.pointerId = c.id;
        p.pointerFlags = POINTER_FLAG_CONFIDENCE | (active ? POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT : 0);
        p.ptHimetricLocation = {static_cast<LONG>(c.x), static_cast<LONG>(c.y)};
        p.dwTime = timestamp;
        return info;
      };
      bool removed = false;
      for (unsigned i = 0; i < previous.count; ++i) {
        bool active = false;
        for (unsigned j = 0; j < next.count; ++j) {
          if (previous.contacts[i].id == next.contacts[j].id) {
            active = true;
          }
        }
        infos[i] = make_info(previous.contacts[i], active);
        removed |= !active;
      }
      if (removed) {
        if (!InjectSyntheticPointerInput(handle, infos.data(), previous.count)) {
          auto error = GetLastError();
          reset();
          SetLastError(error);
          return false;
        }
        ++timestamp;
      }
      for (unsigned i = 0; i < next.count; ++i) {
        infos[i] = make_info(next.contacts[i], true);
      }
      if (next.count && !InjectSyntheticPointerInput(handle, infos.data(), next.count)) {
        auto error = GetLastError();
        reset();
        SetLastError(error);
        return false;
      }
      previous = next;
      sequence = next.sequence;
      ordered = true;
      last_time = timestamp;
      return true;
    }
  };
#endif
}  // namespace precision_touchpad
