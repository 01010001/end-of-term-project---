#include <gtk/gtk.h>
#include <stdlib.h>

GtkWidget *tree_view_files;
GtkWidget *list_box_versions;
GtkWidget *text_view_preview;
GtkWidget *window;
GtkWidget *stack;

/* Version Details Labels */
GtkWidget *lbl_val_name;
GtkWidget *lbl_val_size;
GtkWidget *lbl_val_ctime;
GtkWidget *lbl_val_dtime;

/* Checkout Onay Dialogu */
static void on_checkout_clicked(GtkWidget *widget, gpointer data) {
    const gchar *filename = (const gchar *)data;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Bu versiyona (%s) geri dönmek (Restore) istediğinize emin misiniz?\n\nMevcut durum otomatik olarak yeni bir versiyon olarak kaydedilecektir.", filename);
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        GtkWidget *info = gtk_message_dialog_new(GTK_WINDOW(window),
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_INFO,
                                                 GTK_BUTTONS_OK,
                                                 "Başarılı: Dosya durumu geri döndürüldü.\n(Undo seçeneği mevcuttur)");
        gtk_dialog_run(GTK_DIALOG(info));
        gtk_widget_destroy(info);
    }
}

/* Diff Penceresi */
static void on_diff_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *diff_dialog = gtk_dialog_new_with_buttons("Karşılaştırma (Diff)", 
                                                         GTK_WINDOW(window), 
                                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                         "Kapat", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(diff_dialog), 800, 500);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(diff_dialog));
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add(GTK_CONTAINER(content_area), paned);

    /* Sol - Eski Versiyon */
    GtkWidget *scroll_left = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *text_left = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_left), FALSE);
    GtkTextBuffer *buf_left = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_left));
    gtk_text_buffer_set_text(buf_left, "eski_kod_satiri(void) {\n- silinen satir;\n}", -1);
    gtk_container_add(GTK_CONTAINER(scroll_left), text_left);
    gtk_paned_pack1(GTK_PANED(paned), scroll_left, TRUE, FALSE);

    /* Sağ - Yeni Versiyon */
    GtkWidget *scroll_right = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *text_right = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_right), FALSE);
    GtkTextBuffer *buf_right = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_right));
    gtk_text_buffer_set_text(buf_right, "yeni_kod_satiri(void) {\n+ eklenen satir;\n}", -1);
    gtk_container_add(GTK_CONTAINER(scroll_right), text_right);
    gtk_paned_pack2(GTK_PANED(paned), scroll_right, TRUE, FALSE);

    gtk_widget_show_all(diff_dialog);
    gtk_dialog_run(GTK_DIALOG(diff_dialog));
    gtk_widget_destroy(diff_dialog);
}

/* Orta Panel Timeline Seçimi -> Sağ Paneli Güncelle */
static void on_version_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    if (!row) return;

    /* Index'e göre bilgileri güncelle (Mockup verileri) */
    int index = gtk_list_box_row_get_index(row);
    if (index == 0) { // Current
        gtk_label_set_text(GTK_LABEL(lbl_val_name), "filename1.txt");
        gtk_label_set_text(GTK_LABEL(lbl_val_size), "14 KB");
        gtk_label_set_text(GTK_LABEL(lbl_val_ctime), "19/08/2025 at 23:45");
        gtk_label_set_text(GTK_LABEL(lbl_val_dtime), "-");
    } else if (index == 2) { // Middle
        gtk_label_set_text(GTK_LABEL(lbl_val_name), "filename1");
        gtk_label_set_text(GTK_LABEL(lbl_val_size), "12 KB");
        gtk_label_set_text(GTK_LABEL(lbl_val_ctime), "19/08/2025 at 23:43");
        gtk_label_set_text(GTK_LABEL(lbl_val_dtime), "-");
    } else if (index == 4) { // Bottom
        gtk_label_set_text(GTK_LABEL(lbl_val_name), "filename2.md");
        gtk_label_set_text(GTK_LABEL(lbl_val_size), "8 KB");
        gtk_label_set_text(GTK_LABEL(lbl_val_ctime), "11/01/2025 at 09:20");
        gtk_label_set_text(GTK_LABEL(lbl_val_dtime), "19/08/2025 at 23:40");
    }
}

/* Orta Panele (Timeline/Freeview) Mockup Veri Ekler */
static void populate_timeline() {
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(list_box_versions));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    const char *names[] = {"filename1.txt", "filename1", "filename2.md"};
    const char *dates[] = {"Current", "19/08/2025 at 23:43", "11/01/2025 at 09:20"};

    for (int i = 0; i < 3; i++) {
        /* Add Up Arrow between nodes (except for the very first top node) */
        if (i > 0) {
            GtkWidget *arrow_row = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(arrow_row), FALSE);
            GtkWidget *arrow_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            GtkWidget *arrow_icon = gtk_image_new_from_icon_name("pan-up-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
            gtk_widget_set_margin_top(arrow_icon, 5);
            gtk_widget_set_margin_bottom(arrow_icon, 5);
            gtk_box_pack_start(GTK_BOX(arrow_box), arrow_icon, TRUE, FALSE, 0);
            gtk_container_add(GTK_CONTAINER(arrow_row), arrow_box);
            gtk_list_box_insert(GTK_LIST_BOX(list_box_versions), arrow_row, -1);
        }

        /* Create Node Row */
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
        gtk_widget_set_margin_top(box, 10);
        gtk_widget_set_margin_bottom(box, 10);
        gtk_widget_set_margin_start(box, 20);
        gtk_widget_set_margin_end(box, 20);

        /* Blue Circle Label */
        GtkWidget *lbl_node = gtk_label_new(names[i]);
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_node), "blue-node");
        gtk_widget_set_halign(lbl_node, GTK_ALIGN_CENTER);

        /* Date / Current Label */
        GtkWidget *lbl_date = gtk_label_new(dates[i]);
        gtk_widget_set_halign(lbl_date, GTK_ALIGN_END);

        gtk_box_pack_start(GTK_BOX(box), lbl_node, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(box), lbl_date, FALSE, FALSE, 0);
        
        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_list_box_insert(GTK_LIST_BOX(list_box_versions), row, -1);
    }
    gtk_widget_show_all(list_box_versions);
}

/* Sol Panelden Dosya Seçimi */
static void on_file_selected(GtkTreeSelection *selection, gpointer data) {
    GtkTreeIter iter;
    GtkTreeModel *model;
    gchar *filename;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, 0, &filename, -1);
        populate_timeline();
        g_free(filename);
    }
}

/* Menü Değişimi */
static void on_nav_switch(GtkButton *btn, gpointer stack_ptr) {
    const gchar *name = gtk_widget_get_name(GTK_WIDGET(btn));
    gtk_stack_set_visible_child_name(GTK_STACK(stack_ptr), name);
}

/* Çöp Kutusu Görünümü */
static GtkWidget* create_trash_view() {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new("<span size='x-large' weight='bold'>Çöp Kutusu (Trash) Yönetimi</span>");
    gtk_label_set_markup(GTK_LABEL(title), "<span size='x-large' weight='bold'>Çöp Kutusu (Trash) Yönetimi</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkListStore *store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Dosya Adı", gtk_cell_renderer_text_new(), "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Orijinal Konum", gtk_cell_renderer_text_new(), "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Boyut", gtk_cell_renderer_text_new(), "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Silinme Zamanı", gtk_cell_renderer_text_new(), "text", 3, NULL);

    GtkTreeIter iter;
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, "eski_notlar.txt", 1, "/vcfs/docs/", 2, "4 KB", 3, "2026-05-28 10:15", -1);
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, "resim.png", 1, "/vcfs/images/", 2, "1.2 MB", 3, "2026-05-29 08:30", -1);

    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *btn_res = gtk_button_new_with_label("Seçili Dosyayı Geri Yükle");
    GtkWidget *btn_cln = gtk_button_new_with_label("Çöp Kutusunu Boşalt");
    gtk_box_pack_start(GTK_BOX(btn_box), btn_res, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(btn_box), btn_cln, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 0);

    return vbox;
}

/* Ana Time Machine Görünümü */
static GtkWidget* create_main_view() {
    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *hpaned2 = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    
    /* =========================================================================
     * SOL SÜTUN: File Explorer
     * ========================================================================= */
    GtkWidget *box_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(box_left, 200, -1);
    
    GtkWidget *lbl_left_title = gtk_label_new("File Explorer");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_left_title), "title-label");
    gtk_widget_set_margin_top(lbl_left_title, 10);
    gtk_box_pack_start(GTK_BOX(box_left), lbl_left_title, FALSE, FALSE, 0);

    GtkWidget *scroll_files = gtk_scrolled_window_new(NULL, NULL);
    GtkTreeStore *store = gtk_tree_store_new(1, G_TYPE_STRING);
    tree_view_files = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view_files), FALSE);
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Files", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view_files), column);
    
    GtkTreeIter iter_file;
    gtk_tree_store_append(store, &iter_file, NULL);
    gtk_tree_store_set(store, &iter_file, 0, "File 1", -1);
    gtk_tree_store_append(store, &iter_file, NULL);
    gtk_tree_store_set(store, &iter_file, 0, "File 2", -1);
    gtk_tree_store_append(store, &iter_file, NULL);
    gtk_tree_store_set(store, &iter_file, 0, "File 3", -1);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view_files));
    g_signal_connect(selection, "changed", G_CALLBACK(on_file_selected), NULL);

    gtk_container_add(GTK_CONTAINER(scroll_files), tree_view_files);
    gtk_box_pack_start(GTK_BOX(box_left), scroll_files, TRUE, TRUE, 0);
    
    /* =========================================================================
     * ORTA SÜTUN: Freeview (Timeline)
     * ========================================================================= */
    GtkWidget *box_mid = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(box_mid, 350, -1);

    GtkWidget *lbl_mid_title = gtk_label_new("Freeview");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_mid_title), "title-label");
    gtk_widget_set_margin_top(lbl_mid_title, 10);
    gtk_box_pack_start(GTK_BOX(box_mid), lbl_mid_title, FALSE, FALSE, 0);

    GtkWidget *scroll_versions = gtk_scrolled_window_new(NULL, NULL);
    list_box_versions = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_versions), GTK_SELECTION_SINGLE);
    g_signal_connect(list_box_versions, "row-selected", G_CALLBACK(on_version_row_selected), NULL);
    
    gtk_container_add(GTK_CONTAINER(scroll_versions), list_box_versions);
    gtk_box_pack_start(GTK_BOX(box_mid), scroll_versions, TRUE, TRUE, 0);

    /* =========================================================================
     * SAĞ SÜTUN: Version Details
     * ========================================================================= */
    GtkWidget *box_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(box_right, 350, -1);
    gtk_widget_set_margin_start(box_right, 10);
    gtk_widget_set_margin_end(box_right, 10);

    GtkWidget *lbl_right_title = gtk_label_new("Version Details");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_right_title), "title-label");
    gtk_widget_set_margin_top(lbl_right_title, 10);
    gtk_box_pack_start(GTK_BOX(box_right), lbl_right_title, FALSE, FALSE, 0);

    /* Grid for Metadata */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 15);

    GtkWidget *lbl_name = gtk_label_new("<b>Name:</b>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_name), TRUE);
    gtk_widget_set_halign(lbl_name, GTK_ALIGN_START);
    lbl_val_name = gtk_label_new("-");
    gtk_widget_set_halign(lbl_val_name, GTK_ALIGN_START);

    GtkWidget *lbl_size = gtk_label_new("<b>Size:</b>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_size), TRUE);
    gtk_widget_set_halign(lbl_size, GTK_ALIGN_START);
    lbl_val_size = gtk_label_new("-");
    gtk_widget_set_halign(lbl_val_size, GTK_ALIGN_START);

    GtkWidget *lbl_ctime = gtk_label_new("<b>Creation Time:</b>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_ctime), TRUE);
    gtk_widget_set_halign(lbl_ctime, GTK_ALIGN_START);
    lbl_val_ctime = gtk_label_new("-");
    gtk_widget_set_halign(lbl_val_ctime, GTK_ALIGN_START);

    GtkWidget *lbl_dtime = gtk_label_new("<b>Deletion Time:</b>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_dtime), TRUE);
    gtk_widget_set_halign(lbl_dtime, GTK_ALIGN_START);
    lbl_val_dtime = gtk_label_new("-");
    gtk_widget_set_halign(lbl_val_dtime, GTK_ALIGN_START);

    gtk_grid_attach(GTK_GRID(grid), lbl_name, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_val_name, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_size, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_val_size, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_ctime, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_val_ctime, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_dtime, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_val_dtime, 1, 3, 1, 1);

    gtk_box_pack_start(GTK_BOX(box_right), grid, FALSE, FALSE, 10);

    /* Action Buttons (Restore / Diff) */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *btn_restore = gtk_button_new_with_label("Restore Version");
    GtkWidget *btn_diff = gtk_button_new_with_label("Compare (Diff)");
    g_signal_connect(btn_restore, "clicked", G_CALLBACK(on_checkout_clicked), (gpointer)"Selected File");
    g_signal_connect(btn_diff, "clicked", G_CALLBACK(on_diff_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), btn_restore, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), btn_diff, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_right), btn_box, FALSE, FALSE, 10);

    /* Preview Label */
    GtkWidget *lbl_preview = gtk_label_new("Preview");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_preview), "title-label");
    gtk_widget_set_halign(lbl_preview, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box_right), lbl_preview, FALSE, FALSE, 5);

    /* Preview Text Area (Lorem Ipsum) */
    GtkWidget *scroll_preview = gtk_scrolled_window_new(NULL, NULL);
    text_view_preview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_preview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view_preview), GTK_WRAP_WORD);
    
    GtkTextBuffer *buf_preview = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_preview));
    gtk_text_buffer_set_text(buf_preview, "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Praesent ac sem lacus. Mauris vitae sem lobortis, feugiat ipsum nec, fringilla justo. Nunc vel leo orci. Etiam maximus luctus tortor egestas placerat. Ut pharetra feugiat sollicitudin. Aliquam risus sem, interdum et tincidunt ultricies, consectetur at ex. Integer efficitur iaculis mollis. Etiam gravida, purus id molestie aliquet, nunc massa elementum est, in aliquam massa magna eu dui. Praesent at mattis nunc. Pellentesque imperdiet varius dolor, ut tristique arcu porta nec. Vestibulum vel felis id enim tincidunt ultr.", -1);
    
    gtk_container_add(GTK_CONTAINER(scroll_preview), text_view_preview);
    gtk_box_pack_start(GTK_BOX(box_right), scroll_preview, TRUE, TRUE, 0);

    /* Panelleri Birleştir */
    gtk_paned_pack1(GTK_PANED(hpaned2), box_mid, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(hpaned2), box_right, TRUE, FALSE);
    gtk_paned_pack1(GTK_PANED(hpaned), box_left, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(hpaned), hpaned2, TRUE, FALSE);

    return hpaned;
}

static void activate(GtkApplication *app, gpointer user_data) {
    /* 1. CSS Provider Yükle (Mavi Düğüm Tasarımı İçin) */
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
        "  font-size: 16pt; "
        "  font-weight: bold; "
        "  padding-bottom: 10px; "
        "}";
    gtk_css_provider_load_from_data(cssProvider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(cssProvider),
                                              GTK_STYLE_PROVIDER_PRIORITY_USER);

    /* Ana Pencere */
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "VCFS - Time Machine");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);

    /* Ana Yerleşim (Header + İçerik) */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* Üst Navigasyon Çubuğu */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(header, 10);
    gtk_widget_set_margin_bottom(header, 10);
    GtkWidget *btn_main = gtk_button_new_with_label("Zaman Çizelgesi (Time Machine)");
    gtk_widget_set_name(btn_main, "view_main");
    GtkWidget *btn_trash = gtk_button_new_with_label("Çöp Kutusu (Trash)");
    gtk_widget_set_name(btn_trash, "view_trash");
    
    gtk_box_pack_start(GTK_BOX(header), btn_main, TRUE, TRUE, 10);
    gtk_box_pack_start(GTK_BOX(header), btn_trash, TRUE, TRUE, 10);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    /* Stack (Görünümler Arası Geçiş) */
    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    
    gtk_stack_add_named(GTK_STACK(stack), create_main_view(), "view_main");
    gtk_stack_add_named(GTK_STACK(stack), create_trash_view(), "view_trash");
    gtk_box_pack_start(GTK_BOX(vbox), stack, TRUE, TRUE, 0);

    /* Navigasyon Sinyalleri */
    g_signal_connect(btn_main, "clicked", G_CALLBACK(on_nav_switch), stack);
    g_signal_connect(btn_trash, "clicked", G_CALLBACK(on_nav_switch), stack);

    gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.bitirme.vcfs.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}