#include <gtk/gtk.h>
#include <udisks/udisks.h>

// 状态结构体，增加了 has_media 和 drive_exists
typedef struct {
    GtkWidget *window;
    GtkWidget *status_label;
    GtkWidget *mount_button;
    GtkWidget *open_button;
    GtkWidget *unmount_button;

    UDisksClient *client;
    gchar *mount_point;
    gboolean is_mounted;
    gboolean has_media;
    gboolean drive_exists;
} AppState;

// 前向函数声明
static void update_ui(AppState *state);
static gboolean check_drive_status(gpointer user_data);

// 直接获取写死的光驱对象
UDisksObject* get_drive_object(UDisksClient *client) {
    const gchar *object_path = "/org/freedesktop/UDisks2/block_devices/sr0";
    return udisks_client_get_object(client, object_path);
}

static void on_open_clicked(GtkWidget *widget, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    if (state->is_mounted && state->mount_point) {
        GFile *file = g_file_new_for_path(state->mount_point);
        g_app_info_launch_default_for_uri(g_file_get_uri(file), NULL, NULL);
        g_object_unref(file);
    }
}

static void on_unmount_clicked(GtkWidget *widget, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    UDisksObject *object = get_drive_object(state->client);
    if (!object) {
        g_warning("卸载失败: 找不到光驱对象 /dev/sr0");
        return;
    }
    
    UDisksFilesystem *fs = udisks_object_get_filesystem(object);
    if (!fs) {
        g_warning("卸载失败: 在 /dev/sr0 上找不到文件系统接口");
        g_object_unref(object);
        return;
    }
    
    GError *error = NULL;
    GVariant *options = g_variant_new("a{sv}", NULL);
    
    if (!udisks_filesystem_call_unmount_sync(fs, options, NULL, &error)) {
        g_warning("卸载操作失败: %s", error ? error->message : "未知错误");
        if (error) g_error_free(error);
    }
    
    g_variant_unref(options);
    check_drive_status(state); // 重新检查状态以更新UI
    
    g_object_unref(fs);
    g_object_unref(object);
}

static void on_mount_clicked(GtkWidget *widget, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    UDisksObject *object = get_drive_object(state->client);
    if (!object) {
        g_warning("挂载失败: 找不到光驱对象 /dev/sr0");
        return;
    }
    
    UDisksFilesystem *fs = udisks_object_get_filesystem(object);
    if (!fs) {
        g_warning("挂载失败: 在 /dev/sr0 上找不到文件系统接口");
        g_object_unref(object);
        return;
    }

    gchar *mount_path = NULL;
    GError *error = NULL;
    GVariant *options = g_variant_new("a{sv}", NULL);

    if (!udisks_filesystem_call_mount_sync(fs, options, &mount_path, NULL, &error)) {
        g_warning("挂载操作失败: %s", error ? error->message : "未知错误");
        if (error) g_error_free(error);
    }
    
    g_free(mount_path);
    g_variant_unref(options);
    check_drive_status(state); // 重新检查状态以更新UI
    
    g_object_unref(fs);
    g_object_unref(object);
}

// [核心重构] 此函数现在是唯一的状态检测点
static gboolean check_drive_status(gpointer user_data) {
    AppState *state = (AppState *)user_data;
    
    // 保存之前的状态，用于比较
    gboolean prev_is_mounted = state->is_mounted;
    gboolean prev_has_media = state->has_media;
    gboolean prev_drive_exists = state->drive_exists;

    // 重置当前状态
    gboolean current_drive_exists = FALSE;
    gboolean current_has_media = FALSE;
    gboolean current_is_mounted = FALSE;
    
    UDisksObject *object = get_drive_object(state->client);
    if (!object) {
        // 光驱对象不存在
        current_drive_exists = FALSE;
        current_has_media = FALSE;
        current_is_mounted = FALSE;
    } else {
        current_drive_exists = TRUE;
        UDisksBlock *block = udisks_object_get_block(object);
        if (block) {
            // [兼容性修复] udisks_block_get_hint_has_media() 在旧版库中不存在。
            // 使用 udisks_block_get_size() 作为替代方案，如果容量大于0，则说明有介质。
            current_has_media = (udisks_block_get_size(block) > 0);
            g_object_unref(block);
        }

        // 只有在有介质时才检查挂载状态
        if (current_has_media) {
             UDisksFilesystem *fs = udisks_object_get_filesystem(object);
             if (fs) {
                gchar **mount_points = udisks_filesystem_dup_mount_points(fs);
                current_is_mounted = (mount_points != NULL && mount_points[0] != NULL);
                if (current_is_mounted) {
                    g_free(state->mount_point);
                    state->mount_point = g_strdup(mount_points[0]);
                }
                g_strfreev(mount_points);
                g_object_unref(fs);
            }
        }
        g_object_unref(object);
    }
    
    // 如果驱动器未挂载，确保挂载点路径被清空
    if (!current_is_mounted) {
        g_free(state->mount_point);
        state->mount_point = NULL;
    }

    // 更新App状态
    state->drive_exists = current_drive_exists;
    state->has_media = current_has_media;
    state->is_mounted = current_is_mounted;

    // 仅在任何状态发生变化时才调用UI更新，避免不必要的刷新
    if (prev_drive_exists != current_drive_exists || prev_has_media != current_has_media || prev_is_mounted != current_is_mounted) {
        update_ui(state);
    }

    return G_SOURCE_CONTINUE;
}

// [核心重构] UI更新函数现在只依赖于AppState，不直接查询系统
static void update_ui(AppState *state) {
    if (!state->drive_exists) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "未在系统中找到 /dev/sr0");
        gtk_widget_set_sensitive(state->mount_button, FALSE);
        gtk_widget_show(state->mount_button);
        gtk_widget_hide(state->open_button);
        gtk_widget_hide(state->unmount_button);
    } else if (state->is_mounted) {
        char label_text[256];
        snprintf(label_text, sizeof(label_text), "已挂载到:\n%s", state->mount_point ? state->mount_point : "未知位置");
        gtk_label_set_text(GTK_LABEL(state->status_label), label_text);
        gtk_widget_hide(state->mount_button);
        gtk_widget_show(state->open_button);
        gtk_widget_show(state->unmount_button);
        gtk_widget_set_sensitive(state->unmount_button, TRUE);
        gtk_widget_set_sensitive(state->open_button, TRUE);
    } else { // Drive exists, but not mounted
        if (state->has_media) {
            gtk_label_set_text(GTK_LABEL(state->status_label), "光驱 (/dev/sr0) 有介质，未挂载");
            gtk_widget_set_sensitive(state->mount_button, TRUE);
        } else {
            gtk_label_set_text(GTK_LABEL(state->status_label), "光驱 (/dev/sr0) 中没有介质");
            gtk_widget_set_sensitive(state->mount_button, FALSE);
        }
        gtk_widget_show(state->mount_button);
        gtk_widget_hide(state->open_button);
        gtk_widget_hide(state->unmount_button);
    }
}


static void on_activate(GtkApplication *app, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    
    state->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state->window), "光驱管理器");
    
    // [新功能] 设置窗口图标
    // "drive-optical" 是一个标准图标名称，代表光驱设备
    gtk_window_set_icon_name(GTK_WINDOW(state->window), "drive-optical");
    
    gtk_window_set_default_size(GTK_WINDOW(state->window), 300, 180);
    gtk_window_set_resizable(GTK_WINDOW(state->window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(state->window), 15);

    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(state->window), grid);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(grid, GTK_ALIGN_CENTER);

    state->status_label = gtk_label_new("正在检测状态...");
    gtk_label_set_justify(GTK_LABEL(state->status_label), GTK_JUSTIFY_CENTER);
    gtk_grid_attach(GTK_GRID(grid), state->status_label, 0, 0, 2, 1);
    
    state->mount_button = gtk_button_new_with_label("挂载光驱");
    g_signal_connect(state->mount_button, "clicked", G_CALLBACK(on_mount_clicked), state);
    gtk_grid_attach(GTK_GRID(grid), state->mount_button, 0, 1, 2, 1);
    
    state->open_button = gtk_button_new_with_label("打开文件夹");
    g_signal_connect(state->open_button, "clicked", G_CALLBACK(on_open_clicked), state);
    gtk_grid_attach(GTK_GRID(grid), state->open_button, 0, 1, 1, 1);

    state->unmount_button = gtk_button_new_with_label("卸载光驱");
    g_signal_connect(state->unmount_button, "clicked", G_CALLBACK(on_unmount_clicked), state);
    gtk_grid_attach(GTK_GRID(grid), state->unmount_button, 1, 1, 1, 1);
    
    // 初始检查并更新UI
    check_drive_status(state);
    
    // 每2秒轮询一次状态
    g_timeout_add_seconds(2, check_drive_status, state);
    
    gtk_widget_show_all(state->window);
    
    // on_activate 结尾不再需要调用 update_ui，因为它已在 check_drive_status 中被调用
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;
    
    AppState state = {0}; 
    
    GError *error = NULL;
    state.client = udisks_client_new_sync(NULL, &error);
    if (!state.client) {
        g_printerr("错误: 无法连接到 UDisks2 服务: %s\n", error ? error->message : "未知错误");
        if (error) g_error_free(error);
        return 1;
    }

    app = gtk_application_new("org.kylin.cdmanager", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &state);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    g_object_unref(state.client);
    g_free(state.mount_point);

    return status;
}
