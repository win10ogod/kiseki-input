#include "platform/capture/wayland_portal_capture.hpp"

#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib.h>
#include <unistd.h>

#include "platform/capture/bitmap_writer.hpp"

namespace kiseki::platform::capture {

namespace {

constexpr int kPortalTimeoutMs = 60000;

CaptureResult fail_capture(const std::filesystem::path& path, std::string error) {
    return CaptureResult{
        .ok = false,
        .code = 2,
        .output_path = path,
        .width = 0,
        .height = 0,
        .error = std::move(error),
    };
}

std::string take_glib_error(GError* error) {
    if (error == nullptr) {
        return "unknown GLib error";
    }
    std::string message = error->message != nullptr ? error->message : "unknown GLib error";
    g_error_free(error);
    return message;
}

bool has_wayland_session() {
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return wayland != nullptr && wayland[0] != '\0';
}

std::string next_handle_token() {
    static std::atomic<unsigned long long> counter{0};
    std::ostringstream token;
    token << "kiseki_" << static_cast<unsigned long long>(getpid()) << '_' << counter.fetch_add(1);
    return token.str();
}

struct PortalResponse {
    GMainLoop* loop = nullptr;
    std::string request_path;
    std::string uri;
    guint32 response = 2;
    bool received = false;
};

gboolean quit_portal_loop(gpointer data) {
    auto* response = static_cast<PortalResponse*>(data);
    if (response != nullptr && response->loop != nullptr) {
        g_main_loop_quit(response->loop);
    }
    return G_SOURCE_REMOVE;
}

void on_portal_response(
    GDBusConnection*,
    const gchar*,
    const gchar* object_path,
    const gchar*,
    const gchar*,
    GVariant* parameters,
    gpointer user_data) {
    auto* response = static_cast<PortalResponse*>(user_data);
    if (response == nullptr || object_path == nullptr || response->request_path != object_path) {
        return;
    }

    GVariant* results = nullptr;
    guint32 response_code = 2;
    g_variant_get(parameters, "(u@a{sv})", &response_code, &results);
    response->response = response_code;
    response->received = true;

    if (results != nullptr) {
        GVariant* uri = g_variant_lookup_value(results, "uri", G_VARIANT_TYPE_STRING);
        if (uri != nullptr) {
            response->uri = g_variant_get_string(uri, nullptr);
            g_variant_unref(uri);
        }
        g_variant_unref(results);
    }

    if (response->loop != nullptr) {
        g_main_loop_quit(response->loop);
    }
}

bool portal_name_has_owner(GDBusConnection* connection) {
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        connection,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "NameHasOwner",
        g_variant_new("(s)", "org.freedesktop.portal.Desktop"),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,
        &error);
    if (result == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return false;
    }

    gboolean has_owner = FALSE;
    g_variant_get(result, "(b)", &has_owner);
    g_variant_unref(result);
    return has_owner == TRUE;
}

CaptureResult write_pixbuf_as_bmp(const std::filesystem::path& output_path, GdkPixbuf* pixbuf) {
    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar* source = gdk_pixbuf_get_pixels(pixbuf);
    if (width <= 0 || height <= 0 || source == nullptr || channels < 3) {
        return fail_capture(output_path, "Wayland portal screenshot returned an invalid image");
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    for (int y = 0; y < height; ++y) {
        const guchar* row = source + static_cast<std::size_t>(y) * static_cast<std::size_t>(rowstride);
        for (int x = 0; x < width; ++x) {
            const guchar* rgb = row + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
            const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
            pixels[offset + 0] = rgb[2];
            pixels[offset + 1] = rgb[1];
            pixels[offset + 2] = rgb[0];
            pixels[offset + 3] = 0;
        }
    }

    return write_bgra_bmp(output_path, width, height, pixels);
}

} // namespace

bool wayland_portal_capture_available() {
    if (!has_wayland_session()) {
        return false;
    }

    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (connection == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return false;
    }

    const bool available = portal_name_has_owner(connection);
    g_object_unref(connection);
    return available;
}

CaptureResult capture_desktop_bmp_wayland_portal(const std::filesystem::path& output_path) {
    if (!has_wayland_session()) {
        return fail_capture(output_path, "Wayland portal screenshot requires WAYLAND_DISPLAY");
    }

    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (connection == nullptr) {
        return fail_capture(output_path, "failed to connect to the session D-Bus: " + take_glib_error(error));
    }
    if (!portal_name_has_owner(connection)) {
        g_object_unref(connection);
        return fail_capture(output_path, "org.freedesktop.portal.Desktop is not available on the session bus");
    }

    PortalResponse response;
    const guint subscription_id = g_dbus_connection_signal_subscribe(
        connection,
        "org.freedesktop.portal.Desktop",
        "org.freedesktop.portal.Request",
        "Response",
        nullptr,
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_portal_response,
        &response,
        nullptr);

    const std::string handle_token = next_handle_token();
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(handle_token.c_str()));
    g_variant_builder_add(&options, "{sv}", "interactive", g_variant_new_boolean(FALSE));

    GVariant* result = g_dbus_connection_call_sync(
        connection,
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Screenshot",
        "Screenshot",
        g_variant_new("(sa{sv})", "", &options),
        G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NONE,
        kPortalTimeoutMs,
        nullptr,
        &error);
    if (result == nullptr) {
        g_dbus_connection_signal_unsubscribe(connection, subscription_id);
        g_object_unref(connection);
        return fail_capture(output_path, "Wayland portal Screenshot call failed: " + take_glib_error(error));
    }

    const gchar* request_path = nullptr;
    g_variant_get(result, "(&o)", &request_path);
    response.request_path = request_path != nullptr ? request_path : "";
    g_variant_unref(result);

    response.loop = g_main_loop_new(nullptr, FALSE);
    const guint timeout_id = g_timeout_add(kPortalTimeoutMs, quit_portal_loop, &response);
    if (!response.received) {
        g_main_loop_run(response.loop);
    }
    g_source_remove(timeout_id);
    g_main_loop_unref(response.loop);
    response.loop = nullptr;

    g_dbus_connection_signal_unsubscribe(connection, subscription_id);
    g_object_unref(connection);

    if (!response.received) {
        return fail_capture(output_path, "Wayland portal Screenshot timed out waiting for a Response signal");
    }
    if (response.response != 0) {
        return fail_capture(output_path, "Wayland portal Screenshot was denied or cancelled by the compositor");
    }
    if (response.uri.empty()) {
        return fail_capture(output_path, "Wayland portal Screenshot response did not include a file URI");
    }

    error = nullptr;
    gchar* filename = g_filename_from_uri(response.uri.c_str(), nullptr, &error);
    if (filename == nullptr) {
        return fail_capture(output_path, "Wayland portal Screenshot returned an invalid URI: " + take_glib_error(error));
    }

    error = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(filename, &error);
    g_free(filename);
    if (pixbuf == nullptr) {
        return fail_capture(output_path, "failed to decode Wayland portal screenshot image: " + take_glib_error(error));
    }

    const auto capture = write_pixbuf_as_bmp(output_path, pixbuf);
    g_object_unref(pixbuf);
    return capture;
}

}
