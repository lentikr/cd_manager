#include <gtk/gtk.h>
#include <udisks/udisks.h>

// 状态结构体
typedef struct {
    GtkWidget *window;
    GtkWidget *status_label;
    GtkWidget *mount_button;
    GtkWidget *open_button;
    GtkWidget *unmount_button;

    UDisksClient *client;
    gchar *mount_point;
    gboolean is_mounted;
} AppState;

// 前向函数声明
static void update_ui(AppState *state);
static gboolean check_drive_status(gpointer user_data);

// 直接获取写死的光驱对象，不再动态发现
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
    if (!object) { g_warning("卸载失败: 找不到光驱对象 /dev/sr0"); return; }
    
    UDisksFilesystem *fs = udisks_object_get_filesystem(object);
    if (!fs) { g_object_unref(object); return; }
    
    GVariant *options = g_variant_new("a{sv}", NULL);
    udisks_filesystem_call_unmount_sync(fs, options, NULL, NULL);
    check_drive_status(state);
    g_object_unref(fs);
    g_object_unref(object);
}

static void on_mount_clicked(GtkWidget *widget, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    UDisksObject *object = get_drive_object(state->client);
    if (!object) { g_warning("挂载失败: 找不到光驱对象 /dev/sr0"); return; }
    
    UDisksFilesystem *fs = udisks_object_get_filesystem(object);
    if (!fs) { g_object_unref(object); return; }

    GVariant *options = g_variant_new("a{sv}", NULL);
    udisks_filesystem_call_mount_sync(fs, options, NULL, NULL, NULL);
    check_drive_status(state);
    g_object_unref(fs);
    g_object_unref(object);
}

static gboolean check_drive_status(gpointer user_data) {
    AppState *state = (AppState *)user_data;
    
    gboolean current_status = FALSE;
    gchar **mount_points = NULL;
    
    UDisksObject *object = get_drive_object(state->client);
    if (!object) {
        current_status = FALSE;
    } else {
        UDisksFilesystem *fs = udisks_object_get_filesystem(object);
        if (fs) {
            mount_points = udisks_filesystem_dup_mount_points(fs);
            current_status = (mount_points != NULL && mount_points[0] != NULL);
            if (current_status) {
                g_free(state->mount_point);
                state->mount_point = g_strdup(mount_points[0]);
            }
            g_strfreev(mount_points);
            g_object_unref(fs);
        }
        g_object_unref(object);
    }
    
    if (state->is_mounted != current_status) {
        state->is_mounted = current_status;
        update_ui(state);
    } else if (state->is_mounted) {
        update_ui(state);
    }

    return G_SOURCE_CONTINUE;
}

static void update_ui(AppState *state) {
    UDisksObject *object = get_drive_object(state->client);
    if (!object){
        gtk_label_set_text(GTK_LABEL(state->status_label), "未在系统中找到/dev/sr0");
        gtk_widget_set_sensitive(state->mount_button, FALSE);
        gtk_widget_hide(state->open_button);
        gtk_widget_hide(state->unmount_button);
        return;
    }
    g_object_unref(object);
    gtk_widget_set_sensitive(state->mount_button, TRUE);
    
    if (state->is_mounted) {
        char label_text[256];
        snprintf(label_text, sizeof(label_text), "已挂载到:\n%s", state->mount_point);
        gtk_label_set_text(GTK_LABEL(state->status_label), label_text);
        gtk_widget_hide(state->mount_button);
        gtk_widget_show(state->open_button);
        gtk_widget_show(state->unmount_button);
    } else {
        gtk_label_set_text(GTK_LABEL(state->status_label), "光驱 (/dev/sr0) 未挂载");
        gtk_widget_show(state->mount_button);
        gtk_widget_hide(state->open_button);
        gtk_widget_hide(state->unmount_button);
    }
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    
    state->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state->window), "光驱管理器");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 250, 150);
    gtk_window_set_resizable(GTK_WINDOW(state->window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(state->window), 10);

    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(state->window), grid);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

    state->status_label = gtk_label_new("正在检测状态...");
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

    check_drive_status(state);

    g_timeout_add_seconds(2, check_drive_status, state);
    
    gtk_widget_show_all(state->window);
    update_ui(state);
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;
    
    AppState state = {0}; 
    
    state.client = udisks_client_new_sync(NULL, NULL);
    if (!state.client) {
        g_printerr("错误: 无法连接到 UDisks2 服务。\n");
        return 1;
    }

    app = gtk_application_new("org.example.cdmanager", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &state);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    g_object_unref(state.client);
    g_free(state.mount_point);

    return status;
}
