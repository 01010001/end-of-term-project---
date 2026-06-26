#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <linux/types.h>

/* =========================================================================
 * VCFS IOCTL DEFINITIONS (must match kernel module)
 * ========================================================================= */
#define VCFS_IOCTL_MAGIC 'v'

#define VCFS_IOC_GET_VERSION_COUNT _IOR(VCFS_IOCTL_MAGIC, 1, __u32)

struct vcfs_ioctl_version_info {
    __u32 version_id;
    __u32 timestamp;
    __u32 size;
    __u32 is_compressed;
};
#define VCFS_IOC_GET_VERSIONS _IOWR(VCFS_IOCTL_MAGIC, 2, struct vcfs_ioctl_version_info)
#define VCFS_IOC_CHECKOUT_VERSION _IOW(VCFS_IOCTL_MAGIC, 4, __u32)

struct vcfs_ioctl_trash_info {
    __u32 inode_no;
    __u32 delete_timestamp;
    __u32 size;
    __u32 uid;
    char filename[32];
};

struct vcfs_ioctl_trash_list_args {
    __u32 count;
    struct vcfs_ioctl_trash_info *items;
};

#define VCFS_IOC_GET_TRASH_COUNT _IOR(VCFS_IOCTL_MAGIC, 5, __u32)
#define VCFS_IOC_GET_TRASH_LIST  _IOWR(VCFS_IOCTL_MAGIC, 6, struct vcfs_ioctl_trash_list_args)
#define VCFS_IOC_RESTORE_TRASH   _IOW(VCFS_IOCTL_MAGIC, 7, __u32)
#define VCFS_IOC_CLEAN_TRASH     _IO(VCFS_IOCTL_MAGIC, 8)

struct vcfs_ioctl_read_args {
    __u32 version_id;
    char *buf;
    __u32 count;
};
#define VCFS_IOC_READ_VERSION _IOWR(VCFS_IOCTL_MAGIC, 9, struct vcfs_ioctl_read_args)

/* =========================================================================
 * GLOBAL STATE
 * ========================================================================= */
static char g_mount_point[1024] = "/mnt";
static char g_current_dir[2048] = "";   /* Currently browsed directory */
static char g_selected_file[2048] = ""; /* Currently selected file (full path) */

/* Version data for the selected file */
static struct vcfs_ioctl_version_info *g_versions = NULL;
static __u32 g_version_count = 0;
static int g_selected_version_index = -1;

/* Widgets */
static GtkWidget *window;
static GtkWidget *stack;
static GtkWidget *tree_view_files;
static GtkWidget *list_box_versions;
static GtkWidget *text_view_preview;
static GtkWidget *lbl_val_name;
static GtkWidget *lbl_val_size;
static GtkWidget *lbl_val_ctime;
static GtkWidget *lbl_val_versions;
static GtkWidget *lbl_path_bar;
static GtkWidget *trash_tree_view;
static GtkListStore *trash_store;
static GtkWidget *btn_restore_version;
static GtkWidget *btn_diff;

/* Forward declarations */
static void populate_file_list(const char *dir_path);
static void populate_timeline(const char *filepath);
static void refresh_trash_list(void);

/* =========================================================================
 * UTILITY: Show error dialog
 * ========================================================================= */
static void show_error(const char *title, const char *msg) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK, "%s", title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void show_info(const char *title, const char *msg) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK, "%s", title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Format bytes into human-readable string */
static void format_size(char *buf, size_t buflen, __u32 size) {
    if (size >= 1048576)
        snprintf(buf, buflen, "%.1f MB", size / 1048576.0);
    else if (size >= 1024)
        snprintf(buf, buflen, "%.1f KB", size / 1024.0);
    else
        snprintf(buf, buflen, "%u B", size);
}

/* Format timestamp */
static void format_time(char *buf, size_t buflen, __u32 timestamp) {
    if (timestamp == 0) {
        snprintf(buf, buflen, "-");
        return;
    }
    time_t ts = (time_t)timestamp;
    struct tm *tm_info = localtime(&ts);
    strftime(buf, buflen, "%Y-%m-%d %H:%M:%S", tm_info);
}

/* Get icon name for a file */
static const char* get_icon_for_file(const char *filename, int is_dir) {
    if (is_dir) return "folder";
    const char *dot = strrchr(filename, '.');
    if (!dot) return "text-x-generic";
    if (strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0) return "text-x-script";
    if (strcmp(dot, ".py") == 0 || strcmp(dot, ".sh") == 0) return "text-x-script";
    if (strcmp(dot, ".png") == 0 || strcmp(dot, ".jpg") == 0 || strcmp(dot, ".bmp") == 0) return "image-x-generic";
    if (strcmp(dot, ".md") == 0 || strcmp(dot, ".txt") == 0) return "text-x-generic";
    return "text-x-generic";
}

/* =========================================================================
 * FILE EXPLORER: Populate file list from real directory
 * ========================================================================= */
static void populate_file_list(const char *dir_path) {
    GtkTreeStore *store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view_files)));
    gtk_tree_store_clear(store);

    strncpy(g_current_dir, dir_path, sizeof(g_current_dir) - 1);
    g_current_dir[sizeof(g_current_dir) - 1] = '\0';

    /* Update path bar */
    if (lbl_path_bar) {
        char display_path[256];
        /* Show relative to mount point */
        if (strlen(dir_path) > strlen(g_mount_point)) {
            snprintf(display_path, sizeof(display_path), "/ %s", dir_path + strlen(g_mount_point) + 1);
        } else {
            snprintf(display_path, sizeof(display_path), "/ (root)");
        }
        gtk_label_set_text(GTK_LABEL(lbl_path_bar), display_path);
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        show_error("Cannot Open Directory", strerror(errno));
        return;
    }

    /* If not at mount root, add ".." entry */
    if (strcmp(dir_path, g_mount_point) != 0) {
        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, "go-up", 1, "..", -1);
    }

    struct dirent *entry;
    /* First pass: directories */
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            GtkTreeIter iter;
            gtk_tree_store_append(store, &iter, NULL);
            gtk_tree_store_set(store, &iter, 0, "folder", 1, entry->d_name, -1);
        }
    }

    /* Second pass: files */
    rewinddir(dir);
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISREG(st.st_mode)) {
            GtkTreeIter iter;
            gtk_tree_store_append(store, &iter, NULL);
            gtk_tree_store_set(store, &iter, 0,
                get_icon_for_file(entry->d_name, 0), 1, entry->d_name, -1);
        }
    }
    closedir(dir);
}

/* =========================================================================
 * VERSION TIMELINE: Query kernel for real version data
 * ========================================================================= */
static void populate_timeline(const char *filepath) {
    /* Clear old data */
    if (g_versions) {
        free(g_versions);
        g_versions = NULL;
    }
    g_version_count = 0;
    g_selected_version_index = -1;

    /* Clear the list box */
    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box_versions));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    /* Clear details panel */
    gtk_label_set_text(GTK_LABEL(lbl_val_name), "-");
    gtk_label_set_text(GTK_LABEL(lbl_val_size), "-");
    gtk_label_set_text(GTK_LABEL(lbl_val_ctime), "-");
    gtk_label_set_text(GTK_LABEL(lbl_val_versions), "-");
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_preview));
    gtk_text_buffer_set_text(buf, "", -1);
    gtk_widget_set_sensitive(btn_restore_version, FALSE);
    gtk_widget_set_sensitive(btn_diff, FALSE);

    if (!filepath || strlen(filepath) == 0) return;

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        show_error("Cannot Open File", strerror(errno));
        return;
    }

    /* Get version count */
    __u32 count = 0;
    if (ioctl(fd, VCFS_IOC_GET_VERSION_COUNT, &count) < 0) {
        close(fd);
        show_error("IOCTL Error", "Could not query version count.\nIs this file on a VCFS mount?");
        return;
    }

    if (count == 0) {
        close(fd);
        return;
    }

    /* Get version details */
    g_versions = malloc(sizeof(struct vcfs_ioctl_version_info) * count);
    if (!g_versions) {
        close(fd);
        return;
    }

    if (ioctl(fd, VCFS_IOC_GET_VERSIONS, g_versions) < 0) {
        free(g_versions);
        g_versions = NULL;
        close(fd);
        show_error("IOCTL Error", "Could not retrieve version history.");
        return;
    }
    g_version_count = count;
    close(fd);

    /* Update version count label */
    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%u", g_version_count);
    gtk_label_set_text(GTK_LABEL(lbl_val_versions), count_str);

    /* Populate the timeline list */
    for (__u32 i = 0; i < g_version_count; i++) {
        /* Add arrow separator between nodes */
        if (i > 0) {
            GtkWidget *arrow_row = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(arrow_row), FALSE);
            GtkWidget *arrow_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            GtkWidget *arrow_icon = gtk_image_new_from_icon_name("pan-up-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
            gtk_widget_set_margin_top(arrow_icon, 2);
            gtk_widget_set_margin_bottom(arrow_icon, 2);
            gtk_box_pack_start(GTK_BOX(arrow_box), arrow_icon, TRUE, FALSE, 0);
            gtk_container_add(GTK_CONTAINER(arrow_row), arrow_box);
            gtk_list_box_insert(GTK_LIST_BOX(list_box_versions), arrow_row, -1);
        }

        /* Create version node */
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
        gtk_widget_set_margin_top(box, 8);
        gtk_widget_set_margin_bottom(box, 8);
        gtk_widget_set_margin_start(box, 15);
        gtk_widget_set_margin_end(box, 15);

        /* Version label with blue pill style */
        char ver_label[64];
        if (i == 0)
            snprintf(ver_label, sizeof(ver_label), "v%u (Current)", g_versions[i].version_id);
        else
            snprintf(ver_label, sizeof(ver_label), "v%u", g_versions[i].version_id);

        GtkWidget *lbl_ver = gtk_label_new(ver_label);
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_ver), "blue-node");

        /* Size */
        char size_str[32];
        format_size(size_str, sizeof(size_str), g_versions[i].size);
        GtkWidget *lbl_size = gtk_label_new(size_str);

        /* Timestamp */
        char time_str[64];
        format_time(time_str, sizeof(time_str), g_versions[i].timestamp);
        GtkWidget *lbl_time = gtk_label_new(time_str);
        gtk_widget_set_halign(lbl_time, GTK_ALIGN_END);

        gtk_box_pack_start(GTK_BOX(box), lbl_ver, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), lbl_size, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(box), lbl_time, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_list_box_insert(GTK_LIST_BOX(list_box_versions), row, -1);
    }

    gtk_widget_show_all(list_box_versions);
}

/* =========================================================================
 * VERSION SELECTION: Show details + preview
 * ========================================================================= */
static void on_version_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box; (void)user_data;
    if (!row || !g_versions || g_version_count == 0) return;

    /* Map row index to version index (skip arrow rows) */
    int row_idx = gtk_list_box_row_get_index(row);
    int ver_idx = row_idx / 2; /* Every other row is an arrow */
    if (ver_idx < 0 || ver_idx >= (int)g_version_count) return;

    g_selected_version_index = ver_idx;

    /* Update detail labels */
    const char *basename = strrchr(g_selected_file, '/');
    basename = basename ? basename + 1 : g_selected_file;
    gtk_label_set_text(GTK_LABEL(lbl_val_name), basename);

    char size_str[32];
    format_size(size_str, sizeof(size_str), g_versions[ver_idx].size);
    gtk_label_set_text(GTK_LABEL(lbl_val_size), size_str);

    char time_str[64];
    format_time(time_str, sizeof(time_str), g_versions[ver_idx].timestamp);
    gtk_label_set_text(GTK_LABEL(lbl_val_ctime), time_str);

    /* Enable action buttons */
    gtk_widget_set_sensitive(btn_restore_version, ver_idx > 0); /* Can't restore current */
    gtk_widget_set_sensitive(btn_diff, g_version_count >= 2);

    /* Read version content for preview */
    int fd = open(g_selected_file, O_RDONLY);
    if (fd < 0) return;

    char *preview_buf = calloc(1, 4097);
    struct vcfs_ioctl_read_args args;
    args.version_id = g_versions[ver_idx].version_id;
    args.buf = preview_buf;
    args.count = 4096;

    GtkTextBuffer *text_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_preview));

    if (ioctl(fd, VCFS_IOC_READ_VERSION, &args) == 0 && args.count > 0) {
        preview_buf[args.count] = '\0';
        gtk_text_buffer_set_text(text_buf, preview_buf, -1);
    } else {
        gtk_text_buffer_set_text(text_buf, "(Could not read version content)", -1);
    }

    free(preview_buf);
    close(fd);
}

/* =========================================================================
 * FILE SELECTION: Handle file explorer clicks
 * ========================================================================= */
static void on_file_selected(GtkTreeSelection *selection, gpointer data) {
    (void)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    gchar *filename;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 1, &filename, -1);

    if (strcmp(filename, "..") == 0) {
        /* Navigate up */
        char parent[2048];
        strncpy(parent, g_current_dir, sizeof(parent));
        char *last_slash = strrchr(parent, '/');
        if (last_slash && last_slash != parent) {
            *last_slash = '\0';
            /* Don't go above mount point */
            if (strlen(parent) >= strlen(g_mount_point)) {
                populate_file_list(parent);
            }
        }
        g_free(filename);
        return;
    }

    /* Build full path */
    char full_path[4096];
    snprintf(full_path, sizeof(full_path), "%s/%s", g_current_dir, filename);

    struct stat st;
    if (stat(full_path, &st) != 0) {
        g_free(filename);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        /* Navigate into directory */
        populate_file_list(full_path);
    } else if (S_ISREG(st.st_mode)) {
        /* Select file, show versions */
        strncpy(g_selected_file, full_path, sizeof(g_selected_file) - 1);
        g_selected_file[sizeof(g_selected_file) - 1] = '\0';
        populate_timeline(full_path);
    }

    g_free(filename);
}

/* =========================================================================
 * ACTIONS: Restore Version (Checkout)
 * ========================================================================= */
static void on_checkout_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (g_selected_version_index < 0 || !g_versions) return;

    __u32 target_ver = g_versions[g_selected_version_index].version_id;

    char msg[256];
    snprintf(msg, sizeof(msg),
        "Are you sure you want to restore to version v%u?\n\n"
        "The current state will be saved as a new version automatically.", target_ver);

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO, "%s", msg);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES) return;

    int fd = open(g_selected_file, O_RDWR);
    if (fd < 0) {
        show_error("Restore Failed", strerror(errno));
        return;
    }

    if (ioctl(fd, VCFS_IOC_CHECKOUT_VERSION, &target_ver) < 0) {
        close(fd);
        show_error("Restore Failed", "IOCTL checkout failed. Check kernel logs.");
        return;
    }
    close(fd);

    show_info("Version Restored", "File has been successfully restored to the selected version.");

    /* Refresh timeline */
    populate_timeline(g_selected_file);
}

/* =========================================================================
 * ACTIONS: Diff between two versions
 * ========================================================================= */
static void on_diff_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (g_version_count < 2 || strlen(g_selected_file) == 0) return;

    /* Determine which two versions to compare */
    __u32 v1_id, v2_id;
    if (g_selected_version_index >= 0 && g_selected_version_index < (int)g_version_count - 1) {
        v1_id = g_versions[g_selected_version_index + 1].version_id; /* older */
        v2_id = g_versions[g_selected_version_index].version_id;     /* newer */
    } else {
        v1_id = g_versions[g_version_count - 1].version_id; /* oldest */
        v2_id = g_versions[0].version_id;                    /* newest */
    }

    int fd = open(g_selected_file, O_RDONLY);
    if (fd < 0) {
        show_error("Diff Failed", strerror(errno));
        return;
    }

    /* Read both versions */
    char *buf1 = calloc(1, 4097);
    char *buf2 = calloc(1, 4097);
    struct vcfs_ioctl_read_args args;

    args.version_id = v1_id;
    args.buf = buf1;
    args.count = 4096;
    int r1 = ioctl(fd, VCFS_IOC_READ_VERSION, &args);
    __u32 len1 = (r1 == 0) ? args.count : 0;

    args.version_id = v2_id;
    args.buf = buf2;
    args.count = 4096;
    int r2 = ioctl(fd, VCFS_IOC_READ_VERSION, &args);
    __u32 len2 = (r2 == 0) ? args.count : 0;

    close(fd);

    buf1[len1] = '\0';
    buf2[len2] = '\0';

    /* Create diff dialog */
    GtkWidget *diff_dialog = gtk_dialog_new_with_buttons("Version Comparison (Diff)",
        GTK_WINDOW(window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(diff_dialog), 800, 500);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(diff_dialog));
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add(GTK_CONTAINER(content_area), paned);

    /* Left panel: older version */
    GtkWidget *vbox_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    char left_title[64];
    snprintf(left_title, sizeof(left_title), "Version v%u (Older)", v1_id);
    GtkWidget *lbl_left = gtk_label_new(left_title);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_left), "title-label");
    gtk_box_pack_start(GTK_BOX(vbox_left), lbl_left, FALSE, FALSE, 5);

    GtkWidget *scroll_left = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *text_left = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_left), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_left), GTK_WRAP_WORD);
    GtkTextBuffer *tbuf_left = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_left));

    /* Color the text */
    gtk_text_buffer_create_tag(tbuf_left, "deleted", "background", "#FFDDDD", "foreground", "#990000", NULL);
    gtk_text_buffer_create_tag(tbuf_left, "normal", NULL);

    /* Show content line by line, marking lines not in new version */
    GtkTextIter titer;
    gtk_text_buffer_get_iter_at_offset(tbuf_left, &titer, 0);

    char *line1 = buf1;
    while (line1 && *line1) {
        char *nl = strchr(line1, '\n');
        size_t llen = nl ? (size_t)(nl - line1 + 1) : strlen(line1);
        char linebuf[4097];
        memcpy(linebuf, line1, llen);
        linebuf[llen] = '\0';

        /* Check if this line exists in buf2 */
        if (strstr(buf2, linebuf))
            gtk_text_buffer_insert_with_tags_by_name(tbuf_left, &titer, linebuf, -1, "normal", NULL);
        else
            gtk_text_buffer_insert_with_tags_by_name(tbuf_left, &titer, linebuf, -1, "deleted", NULL);

        line1 = nl ? nl + 1 : NULL;
    }

    gtk_container_add(GTK_CONTAINER(scroll_left), text_left);
    gtk_box_pack_start(GTK_BOX(vbox_left), scroll_left, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(paned), vbox_left, TRUE, FALSE);

    /* Right panel: newer version */
    GtkWidget *vbox_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    char right_title[64];
    snprintf(right_title, sizeof(right_title), "Version v%u (Newer)", v2_id);
    GtkWidget *lbl_right = gtk_label_new(right_title);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_right), "title-label");
    gtk_box_pack_start(GTK_BOX(vbox_right), lbl_right, FALSE, FALSE, 5);

    GtkWidget *scroll_right = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *text_right = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_right), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_right), GTK_WRAP_WORD);
    GtkTextBuffer *tbuf_right = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_right));

    gtk_text_buffer_create_tag(tbuf_right, "added", "background", "#DDFFDD", "foreground", "#006600", NULL);
    gtk_text_buffer_create_tag(tbuf_right, "normal", NULL);

    gtk_text_buffer_get_iter_at_offset(tbuf_right, &titer, 0);

    char *line2 = buf2;
    while (line2 && *line2) {
        char *nl = strchr(line2, '\n');
        size_t llen = nl ? (size_t)(nl - line2 + 1) : strlen(line2);
        char linebuf[4097];
        memcpy(linebuf, line2, llen);
        linebuf[llen] = '\0';

        if (strstr(buf1, linebuf))
            gtk_text_buffer_insert_with_tags_by_name(tbuf_right, &titer, linebuf, -1, "normal", NULL);
        else
            gtk_text_buffer_insert_with_tags_by_name(tbuf_right, &titer, linebuf, -1, "added", NULL);

        line2 = nl ? nl + 1 : NULL;
    }

    gtk_container_add(GTK_CONTAINER(scroll_right), text_right);
    gtk_box_pack_start(GTK_BOX(vbox_right), scroll_right, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), vbox_right, TRUE, FALSE);

    free(buf1);
    free(buf2);

    gtk_widget_show_all(diff_dialog);
    gtk_dialog_run(GTK_DIALOG(diff_dialog));
    gtk_widget_destroy(diff_dialog);
}

/* =========================================================================
 * REFRESH BUTTON: Re-scan files
 * ========================================================================= */
static void on_refresh_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    populate_file_list(g_current_dir);
    if (strlen(g_selected_file) > 0)
        populate_timeline(g_selected_file);
}

/* =========================================================================
 * TRASH: Populate from kernel
 * ========================================================================= */
static void refresh_trash_list(void) {
    if (!trash_store) return;
    gtk_list_store_clear(trash_store);

    int dir_fd = open(g_mount_point, O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) return;

    __u32 count = 0;
    if (ioctl(dir_fd, VCFS_IOC_GET_TRASH_COUNT, &count) < 0 || count == 0) {
        close(dir_fd);
        return;
    }

    struct vcfs_ioctl_trash_list_args args;
    args.count = count;
    args.items = malloc(sizeof(struct vcfs_ioctl_trash_info) * count);
    if (!args.items) {
        close(dir_fd);
        return;
    }

    if (ioctl(dir_fd, VCFS_IOC_GET_TRASH_LIST, &args) < 0) {
        free(args.items);
        close(dir_fd);
        return;
    }

    for (__u32 i = 0; i < args.count; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(trash_store, &iter);

        char size_str[32], time_str[64], ino_str[16];
        format_size(size_str, sizeof(size_str), args.items[i].size);
        format_time(time_str, sizeof(time_str), args.items[i].delete_timestamp);
        snprintf(ino_str, sizeof(ino_str), "%u", args.items[i].inode_no);

        gtk_list_store_set(trash_store, &iter,
            0, args.items[i].filename,
            1, size_str,
            2, time_str,
            3, ino_str,
            -1);
    }

    free(args.items);
    close(dir_fd);
}

/* Restore selected trash item */
static void on_trash_restore_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(trash_tree_view));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
        show_error("No Selection", "Please select a file to restore.");
        return;
    }

    gchar *ino_str;
    gtk_tree_model_get(model, &iter, 3, &ino_str, -1);
    __u32 target_ino = (__u32)atoi(ino_str);
    g_free(ino_str);

    int dir_fd = open(g_mount_point, O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        show_error("Restore Failed", strerror(errno));
        return;
    }

    if (ioctl(dir_fd, VCFS_IOC_RESTORE_TRASH, &target_ino) < 0) {
        close(dir_fd);
        show_error("Restore Failed", "IOCTL restore failed. The file may have a name conflict.");
        return;
    }
    close(dir_fd);

    show_info("File Restored", "The file has been successfully restored from trash.");
    refresh_trash_list();
    populate_file_list(g_current_dir);
}

/* Empty all trash */
static void on_trash_clean_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
        GTK_BUTTONS_YES_NO,
        "Are you sure you want to permanently empty the trash?\n\n"
        "This action cannot be undone.");

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES) return;

    int dir_fd = open(g_mount_point, O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        show_error("Clean Failed", strerror(errno));
        return;
    }

    if (ioctl(dir_fd, VCFS_IOC_CLEAN_TRASH) < 0) {
        close(dir_fd);
        show_error("Clean Failed", "IOCTL clean trash failed.");
        return;
    }
    close(dir_fd);

    show_info("Trash Emptied", "All deleted files have been permanently removed.");
    refresh_trash_list();
}

/* =========================================================================
 * NAVIGATION: Switch between views
 * ========================================================================= */
static void on_nav_switch(GtkButton *btn, gpointer stack_ptr) {
    const gchar *name = gtk_widget_get_name(GTK_WIDGET(btn));
    gtk_stack_set_visible_child_name(GTK_STACK(stack_ptr), name);

    /* Refresh trash when switching to trash view */
    if (strcmp(name, "view_trash") == 0)
        refresh_trash_list();
}

/* =========================================================================
 * CREATE VIEWS
 * ========================================================================= */
static GtkWidget* create_trash_view(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    /* Header */
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='x-large' weight='bold'>Trash Management</span>");

    GtkWidget *btn_refresh = gtk_button_new_from_icon_name("view-refresh", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header_box), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);

    /* Trash list */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scroll, TRUE);

    trash_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    trash_tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(trash_store));

    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(trash_tree_view), -1,
        "Filename", gtk_cell_renderer_text_new(), "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(trash_tree_view), -1,
        "Size", gtk_cell_renderer_text_new(), "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(trash_tree_view), -1,
        "Deleted At", gtk_cell_renderer_text_new(), "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(trash_tree_view), -1,
        "Inode", gtk_cell_renderer_text_new(), "text", 3, NULL);

    /* Make columns resizable */
    for (int i = 0; i < 4; i++) {
        GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(trash_tree_view), i);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_column_set_expand(col, i == 0);
    }

    gtk_container_add(GTK_CONTAINER(scroll), trash_tree_view);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    /* Action buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *btn_res = gtk_button_new_with_label("Restore Selected File");
    GtkWidget *btn_cln = gtk_button_new_with_label("Empty Trash");
    g_signal_connect(btn_res, "clicked", G_CALLBACK(on_trash_restore_clicked), NULL);
    g_signal_connect(btn_cln, "clicked", G_CALLBACK(on_trash_clean_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), btn_res, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(btn_box), btn_cln, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 0);

    return vbox;
}

static GtkWidget* create_main_view(void) {
    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *hpaned2 = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

    /* ====================== LEFT: File Explorer ====================== */
    GtkWidget *box_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(box_left, 220, -1);

    /* Header with title + refresh button */
    GtkWidget *left_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(left_header, 10);
    gtk_widget_set_margin_start(left_header, 5);
    gtk_widget_set_margin_end(left_header, 5);
    GtkWidget *lbl_left_title = gtk_label_new("File Explorer");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_left_title), "title-label");
    GtkWidget *btn_refresh = gtk_button_new_from_icon_name("view-refresh", GTK_ICON_SIZE_SMALL_TOOLBAR);
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(left_header), lbl_left_title, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(left_header), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_left), left_header, FALSE, FALSE, 0);

    /* Path bar */
    lbl_path_bar = gtk_label_new("/ (root)");
    gtk_widget_set_halign(lbl_path_bar, GTK_ALIGN_START);
    gtk_widget_set_margin_start(lbl_path_bar, 10);
    gtk_widget_set_margin_bottom(lbl_path_bar, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_path_bar), "dim-label");
    gtk_box_pack_start(GTK_BOX(box_left), lbl_path_bar, FALSE, FALSE, 0);

    /* File tree */
    GtkWidget *scroll_files = gtk_scrolled_window_new(NULL, NULL);
    GtkTreeStore *store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    tree_view_files = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view_files), FALSE);

    GtkTreeViewColumn *column = gtk_tree_view_column_new();
    GtkCellRenderer *icon_renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
    gtk_tree_view_column_add_attribute(column, icon_renderer, "icon-name", 0);
    GtkCellRenderer *text_renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, text_renderer, "text", 1);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view_files), column);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view_files));
    g_signal_connect(selection, "changed", G_CALLBACK(on_file_selected), NULL);

    gtk_container_add(GTK_CONTAINER(scroll_files), tree_view_files);
    gtk_box_pack_start(GTK_BOX(box_left), scroll_files, TRUE, TRUE, 0);

    /* ====================== MIDDLE: Version Timeline ====================== */
    GtkWidget *box_mid = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(box_mid, 350, -1);

    GtkWidget *lbl_mid_title = gtk_label_new("Version Timeline");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_mid_title), "title-label");
    gtk_widget_set_margin_top(lbl_mid_title, 10);
    gtk_box_pack_start(GTK_BOX(box_mid), lbl_mid_title, FALSE, FALSE, 0);

    GtkWidget *scroll_versions = gtk_scrolled_window_new(NULL, NULL);
    list_box_versions = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_versions), GTK_SELECTION_SINGLE);
    g_signal_connect(list_box_versions, "row-selected", G_CALLBACK(on_version_row_selected), NULL);
    gtk_container_add(GTK_CONTAINER(scroll_versions), list_box_versions);
    gtk_box_pack_start(GTK_BOX(box_mid), scroll_versions, TRUE, TRUE, 0);

    /* ====================== RIGHT: Version Details ====================== */
    GtkWidget *box_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(box_right, 350, -1);
    gtk_widget_set_margin_start(box_right, 10);
    gtk_widget_set_margin_end(box_right, 10);

    GtkWidget *lbl_right_title = gtk_label_new("Version Details");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_right_title), "title-label");
    gtk_widget_set_margin_top(lbl_right_title, 10);
    gtk_box_pack_start(GTK_BOX(box_right), lbl_right_title, FALSE, FALSE, 0);

    /* Metadata grid */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 15);

    GtkWidget *lbl_name = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_name), "<b>Name:</b>");
    gtk_widget_set_halign(lbl_name, GTK_ALIGN_START);
    lbl_val_name = gtk_label_new("-");
    gtk_widget_set_halign(lbl_val_name, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(lbl_val_name), PANGO_ELLIPSIZE_MIDDLE);

    GtkWidget *lbl_size = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_size), "<b>Size:</b>");
    gtk_widget_set_halign(lbl_size, GTK_ALIGN_START);
    lbl_val_size = gtk_label_new("-");
    gtk_widget_set_halign(lbl_val_size, GTK_ALIGN_START);

    GtkWidget *lbl_ctime = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_ctime), "<b>Timestamp:</b>");
    gtk_widget_set_halign(lbl_ctime, GTK_ALIGN_START);
    lbl_val_ctime = gtk_label_new("-");
    gtk_widget_set_halign(lbl_val_ctime, GTK_ALIGN_START);

    GtkWidget *lbl_versions = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_versions), "<b>Total Versions:</b>");
    gtk_widget_set_halign(lbl_versions, GTK_ALIGN_START);
    lbl_val_versions = gtk_label_new("-");
    gtk_widget_set_halign(lbl_val_versions, GTK_ALIGN_START);

    gtk_grid_attach(GTK_GRID(grid), lbl_name, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_val_name, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_size, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_val_size, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_ctime, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_val_ctime, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_versions, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_val_versions, 1, 3, 1, 1);
    gtk_box_pack_start(GTK_BOX(box_right), grid, FALSE, FALSE, 10);

    /* Action buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    btn_restore_version = gtk_button_new_with_label("Restore Version");
    btn_diff = gtk_button_new_with_label("Compare (Diff)");
    gtk_widget_set_sensitive(btn_restore_version, FALSE);
    gtk_widget_set_sensitive(btn_diff, FALSE);
    g_signal_connect(btn_restore_version, "clicked", G_CALLBACK(on_checkout_clicked), NULL);
    g_signal_connect(btn_diff, "clicked", G_CALLBACK(on_diff_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), btn_restore_version, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), btn_diff, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_right), btn_box, FALSE, FALSE, 10);

    /* Preview */
    GtkWidget *lbl_preview = gtk_label_new("Content Preview");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_preview), "title-label");
    gtk_widget_set_halign(lbl_preview, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box_right), lbl_preview, FALSE, FALSE, 5);

    GtkWidget *scroll_preview = gtk_scrolled_window_new(NULL, NULL);
    text_view_preview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_preview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view_preview), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scroll_preview), text_view_preview);
    gtk_box_pack_start(GTK_BOX(box_right), scroll_preview, TRUE, TRUE, 0);

    /* Assemble panes */
    gtk_paned_pack1(GTK_PANED(hpaned2), box_mid, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(hpaned2), box_right, TRUE, FALSE);
    gtk_paned_pack1(GTK_PANED(hpaned), box_left, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(hpaned), hpaned2, TRUE, FALSE);

    return hpaned;
}

/* =========================================================================
 * APPLICATION ACTIVATION
 * ========================================================================= */
static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    /* CSS */
    GtkCssProvider *cssProvider = gtk_css_provider_new();
    const gchar *css =
        ".blue-node { "
        "  background-color: #0078D7; "
        "  color: white; "
        "  border-radius: 20px; "
        "  padding: 8px 16px; "
        "  font-weight: bold; "
        "}"
        ".title-label { "
        "  font-size: 14pt; "
        "  font-weight: bold; "
        "  padding-bottom: 5px; "
        "}";
    gtk_css_provider_load_from_data(cssProvider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    /* Main window */
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "VCFS - Time Machine");
    gtk_window_set_default_size(GTK_WINDOW(window), 1100, 650);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* Navigation bar */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(header, 10);
    gtk_widget_set_margin_bottom(header, 10);
    GtkWidget *btn_main = gtk_button_new_with_label("Time Machine");
    gtk_widget_set_name(btn_main, "view_main");
    GtkWidget *btn_trash = gtk_button_new_with_label("Trash");
    gtk_widget_set_name(btn_trash, "view_trash");

    gtk_box_pack_start(GTK_BOX(header), btn_main, TRUE, TRUE, 10);
    gtk_box_pack_start(GTK_BOX(header), btn_trash, TRUE, TRUE, 10);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    /* Stack */
    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(GTK_STACK(stack), create_main_view(), "view_main");
    gtk_stack_add_named(GTK_STACK(stack), create_trash_view(), "view_trash");
    gtk_box_pack_start(GTK_BOX(vbox), stack, TRUE, TRUE, 0);

    g_signal_connect(btn_main, "clicked", G_CALLBACK(on_nav_switch), stack);
    g_signal_connect(btn_trash, "clicked", G_CALLBACK(on_nav_switch), stack);

    gtk_widget_show_all(window);

    /* Load initial file list */
    populate_file_list(g_mount_point);
}

/* =========================================================================
 * MAIN
 * ========================================================================= */
int main(int argc, char **argv) {
    /* Accept mount point as argument */
    if (argc >= 2) {
        strncpy(g_mount_point, argv[1], sizeof(g_mount_point) - 1);
        g_mount_point[sizeof(g_mount_point) - 1] = '\0';
    }

    /* Remove trailing slash */
    size_t len = strlen(g_mount_point);
    if (len > 1 && g_mount_point[len - 1] == '/')
        g_mount_point[len - 1] = '\0';

    strncpy(g_current_dir, g_mount_point, sizeof(g_current_dir) - 1);

    GtkApplication *app = gtk_application_new("com.vcfs.timemachine", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), 1, argv); /* Pass only program name to GTK */
    g_object_unref(app);

    if (g_versions) free(g_versions);
    return status;
}