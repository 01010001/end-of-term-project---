#include <gtk/gtk.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include "../vcfs-daemon/vcfs_ioctl.h"

/* 
 * VCFS GUI Uygulaması (GTK3 - C Native)
 * Bölüm 3.6 - Grafik Kullanıcı Arayüzü (GUI) Tasarımı
 */

GtkWidget *tree_view_files;
GtkWidget *list_box_versions;
GtkWidget *text_view_preview;

/* Orta Panele (Timeline) Mockup Veri Ekler */
static void populate_timeline(GtkWidget *listbox, int version_count) {
    /* Eski verileri temizle */
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(listbox));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    if (version_count == 0) {
        GtkWidget *label = gtk_label_new("Bu dosya için versiyon bulunamadı.");
        gtk_list_box_insert(GTK_LIST_BOX(listbox), label, -1);
        gtk_widget_show_all(listbox);
        return;
    }

    for (int i = 0; i < version_count; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Versiyon v%d\nTarih: 2026-05-%02d 14:30\nDurum: %s", 
                 version_count - i, 20 - i, (i == 0) ? "Aktif (Head)" : "Yedeklendi");

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *label = gtk_label_new(buf);
        GtkWidget *btn_restore = gtk_button_new_with_label("Geri Dön (Checkout)");
        
        gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 10);
        gtk_box_pack_end(GTK_BOX(box), btn_restore, FALSE, FALSE, 10);
        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_list_box_insert(GTK_LIST_BOX(listbox), row, -1);
    }
    gtk_widget_show_all(listbox);
}

/* Sol Panelden Bir Dosya Seçildiğinde Tetiklenir */
static void on_file_selected(GtkTreeSelection *selection, gpointer data) {
    GtkTreeIter iter;
    GtkTreeModel *model;
    gchar *filename;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, 0, &filename, -1);
        
        /* 
         * Gerçek uygulamada burada dosya IOCTL ile Kernel'den versiyon geçmişini 
         * çekecektir. Mockup amaçlı dosya ismine göre fake data üretiyoruz.
         */
        g_print("Seçilen Dosya: %s\n", filename);
        
        /* Önizleme ekranını güncelle */
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_preview));
        char preview_text[256];
        snprintf(preview_text, sizeof(preview_text), "Dosya: %s\n\n[İçerik Önizlemesi Burada Görüntülenecek]", filename);
        gtk_text_buffer_set_text(buffer, preview_text, -1);

        /* Timeline'ı güncelle */
        populate_timeline(list_box_versions, 5); // 5 versiyonluk fake data

        g_free(filename);
    }
}

static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window;
    GtkWidget *paned_main;
    GtkWidget *paned_right;
    
    GtkWidget *scroll_files;
    GtkWidget *scroll_versions;
    GtkWidget *scroll_preview;

    /* Ana Pencere */
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "VCFS - Time Machine (Versiyon Yöneticisi)");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);

    /* Panelleri Oluştur (3 Sütunlu Tasarım) */
    paned_main = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    paned_right = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    
    gtk_container_add(GTK_CONTAINER(window), paned_main);
    gtk_paned_pack1(GTK_PANED(paned_main), paned_right, TRUE, FALSE);
    
    /* 1. Sol Panel: Dosya Gezgini */
    scroll_files = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll_files, 250, -1);
    
    GtkTreeStore *store = gtk_tree_store_new(1, G_TYPE_STRING);
    tree_view_files = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Takip Edilen Dosyalar", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view_files), column);
    
    /* Fake Dosya Ekleme */
    GtkTreeIter root_iter;
    gtk_tree_store_append(store, &root_iter, NULL);
    gtk_tree_store_set(store, &root_iter, 0, "tez_raporu.docx", -1);
    gtk_tree_store_append(store, &root_iter, NULL);
    gtk_tree_store_set(store, &root_iter, 0, "main.c", -1);
    gtk_tree_store_append(store, &root_iter, NULL);
    gtk_tree_store_set(store, &root_iter, 0, "Makefile", -1);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view_files));
    g_signal_connect(selection, "changed", G_CALLBACK(on_file_selected), NULL);

    gtk_container_add(GTK_CONTAINER(scroll_files), tree_view_files);
    gtk_paned_pack1(GTK_PANED(paned_main), scroll_files, FALSE, FALSE);

    /* 2. Orta Panel: Timeline (Versiyonlar) */
    scroll_versions = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll_versions, 350, -1);
    list_box_versions = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scroll_versions), list_box_versions);
    
    gtk_paned_pack1(GTK_PANED(paned_right), scroll_versions, TRUE, FALSE);

    /* 3. Sağ Panel: Önizleme / Diff */
    scroll_preview = gtk_scrolled_window_new(NULL, NULL);
    text_view_preview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_preview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view_preview), TRUE);
    
    gtk_container_add(GTK_CONTAINER(scroll_preview), text_view_preview);
    gtk_paned_pack2(GTK_PANED(paned_right), scroll_preview, TRUE, FALSE);

    gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    /* Native GTK uygulamasını başlat */
    app = gtk_application_new("com.bitirme.vcfs.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
