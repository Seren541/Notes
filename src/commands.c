#include <gtksourceview/gtksource.h>
#include "global.h"
#include "config.h"

void filename_to_title(struct Document * document) {
    char * append = " - Janus";
    char * p = document->path;
    if (strrchr(document->path, '/') != NULL) {
        p = strrchr(document->path, '/') + 1;
    }
    char * title = malloc(strlen(append) + strlen(p) + 1);
    strcpy(title, p);
    strcat(title, append);
    gtk_window_set_title(GTK_WINDOW(document->window), title);
    free(title);
}

void change_indicator(GtkWidget * self, struct Document * document) {
    //Assumes it's the current tab
    if (gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(self))) {
        const char * current = gtk_window_get_title(document->window);
        char * prepend = "* ";
        char * title = malloc(strlen(current) + strlen(prepend) + 1);
        strcpy(title, prepend);
        strcat(title, current);
        gtk_window_set_title(document->window, title);
        free(title);
    }
    else {
        const char * current = gtk_window_get_title(document->window);
        gtk_window_set_title(document->window, current + 2);
    }
}

void warning_popup(struct Document * document, char * text) {
    GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
    GtkWidget * dialog = gtk_message_dialog_new (document->window, flags, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, text, NULL);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
}

void open_file(char * filename, struct Document * document) {

    char * path;
    path = realpath(filename, NULL);
    if (path == NULL) {
        warning_popup(document, _("Could not resolve path."));
        return;
    }

    char * contents;
    gsize len;
    GError * error = NULL;
    g_file_get_contents(path, &contents, &len, NULL);
    if (error) {
        warning_popup(document, _("Could not open file."));
        g_error_free(error);
        free(path);
        return;
    }

    free(document->path);
    document->path = path;

    gtk_source_buffer_begin_not_undoable_action(GTK_SOURCE_BUFFER(document->buffer));
    g_signal_handlers_block_by_func(document->buffer, change_indicator, document);
    if (g_utf8_validate(contents, len, NULL) == FALSE) {
        document->binary = TRUE;

        // Convert to utf8, and then transform characters such as null to glib approriate characters
        // Ascii characters map normally with the exception of NUL (thanks glib)
        // Characters above 127 map to the unicode U+xxxx counterpart for simplicity wihtout choosing any codepage
        size_t newsize = len;
        for (size_t i = 0; i < len; i++) {
            if (!contents[i]) {
                newsize += 2;
            }
            else if (contents[i] < 0) {
                newsize += 1;
            }
        }

        char * binary_string = malloc(newsize + 1);
        size_t iter = 0;

        for (size_t i = 0; i < len; i++) {
            if (contents[i] > 0) {
                binary_string[iter] = contents[i];
                iter += 1;
            }
            else if (contents[i] < 0){
                binary_string[iter+0] = 0xC0 + (2 * (contents[i] & 0x80 && TRUE)) + (1 * (contents[i] & 0x40 && TRUE));
                binary_string[iter+1] = 0x80 + (unsigned char)contents[i] % 64;
                iter += 2;
            }
            else {
                binary_string[iter+0] = 0xE2;
                binary_string[iter+1] = 0x90;
                binary_string[iter+2] = 0x80;
                iter += 3;
            }
        }
        binary_string[iter] = 0;
        gtk_text_buffer_set_text(document->buffer, binary_string, -1);
        free(binary_string);
    }
    else {
        document->binary = FALSE;
        gtk_text_buffer_set_text(document->buffer, contents, -1);
    }
    gtk_text_buffer_set_modified(document->buffer, FALSE);
    g_signal_handlers_unblock_by_func(document->buffer, change_indicator, document);
    gtk_source_buffer_end_not_undoable_action(GTK_SOURCE_BUFFER(document->buffer));

    free(contents);
    filename_to_title(document);
}

void open_command(GtkWidget * self, struct Document * document) {
    
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Open File"), document->window, action, ("Cancel"), GTK_RESPONSE_CANCEL, ("Open"), GTK_RESPONSE_ACCEPT, NULL);

    gint res = gtk_dialog_run (GTK_DIALOG (dialog));
    if (res == GTK_RESPONSE_ACCEPT)
    {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
        char * filename = gtk_file_chooser_get_filename (chooser);
        open_file(filename, document);
        g_free (filename);
    }

    gtk_widget_destroy (dialog);
}

void new_command(void) {
    GError *err = NULL;
    char* argv[] = {"/proc/self/exe", NULL};
    g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, &err);
    if (err != NULL) {
        puts(err->message);
        g_error_free(err);
    }
}

void save(struct Document * document) {

    // Collect all text
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_start_iter(document->buffer, &start);
    gtk_text_buffer_get_end_iter(document->buffer, &end);

    unsigned char * text = (unsigned char *)gtk_text_buffer_get_text(document->buffer, &start, &end, 0);
    size_t len = -1;

    if (document->binary) {
        size_t iter = 0;
        len++;
        while(text[iter]) {
            if (text[iter] > 127) {
                switch (text[iter]) {
                    case 0xE2:
                        if (text[iter+1] == 0x90 && text[iter+2] == 0x80) {
                            text[len] = 0;
                            iter+=2;
                            break;
                        }
                    case 0xC2:
                        text[len] = text[iter+1];
                        iter++;
                        break;
                    case 0xC3:
                        text[len] = text[iter+1] + 64;
                        iter++;
                        break;
                    default:
                        warning_popup(document, _("File contains non binary characters. Make sure all inserted characters are between U+0000 and U+00FF with excpetion U+2400"));
                        free(text);
                        return;
                }
            }
            else
                text[len] = text[iter];
            iter++;
            len++;
        }
        text[len] = 0;
    }

    GError * error = NULL;
    g_file_set_contents(document->path, (char *)text, len, &error);
    if (error) {
        warning_popup(document, error->message);
        g_error_free(error);
        return;
    }

    gtk_text_buffer_set_modified(document->buffer, FALSE);

}

void save_as_command(GtkWidget * self, struct Document * document) {

    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;
    GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Save File"), document->window, action, ("Cancel"), GTK_RESPONSE_CANCEL, ("Save"), GTK_RESPONSE_ACCEPT, NULL);

    gint res = gtk_dialog_run (GTK_DIALOG (dialog));
    if (res == GTK_RESPONSE_ACCEPT)
    {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
        char * filename = gtk_file_chooser_get_filename (chooser);
        free(document->path);
        document->path = g_strdup(filename);
        g_free (filename);
        save(document);
        filename_to_title(document);
    }

    gtk_widget_destroy (dialog);
}

void save_command(GtkWidget * self, struct Document * document) {

    //If I want to save as file whenever someone presses control s, switch these two if statements around
    if (gtk_text_buffer_get_modified(document->buffer) == FALSE)
        return;

    if (document->path == NULL) {
        save_as_command(self, document);
        return;
    }

    save(document);

}

void draw_page (GtkPrintOperation* self, GtkPrintContext* context, gint page_nr, GtkSourcePrintCompositor *compositor) {
    gtk_source_print_compositor_draw_page (compositor, context, page_nr);
}

static gboolean paginate (GtkPrintOperation *operation, GtkPrintContext *context, GtkSourcePrintCompositor *compositor) {
    if (gtk_source_print_compositor_paginate (compositor, context))
    {
        gint n_pages;

        n_pages = gtk_source_print_compositor_get_n_pages (compositor);
        gtk_print_operation_set_n_pages (operation, n_pages);

        return TRUE;
    }

    return FALSE;
}

void print_command(GtkWidget * self, struct Document * document) {
    GtkPrintOperation * print = gtk_print_operation_new();
    GtkSourcePrintCompositor * compositor = gtk_source_print_compositor_new(GTK_SOURCE_BUFFER(document->buffer));
    
    g_signal_connect (print, "draw_page", G_CALLBACK (draw_page), compositor);
    g_signal_connect (print, "paginate", G_CALLBACK (paginate), compositor);

    if (gtk_print_operation_run(print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, GTK_WINDOW (document->window), NULL) == GTK_PRINT_OPERATION_RESULT_ERROR)
        warning_popup(document, _("Failed to print page."));

    g_object_unref(print);
    g_object_unref(compositor);
}

void print_preview_command(GtkWidget * self, struct Document * document) {
    GtkPrintOperation * print = gtk_print_operation_new();
    GtkSourcePrintCompositor * compositor = gtk_source_print_compositor_new(GTK_SOURCE_BUFFER(document->buffer));
    
    g_signal_connect (print, "draw_page", G_CALLBACK (draw_page), compositor);
    g_signal_connect (print, "paginate", G_CALLBACK (paginate), compositor);

    if (gtk_print_operation_run(print, GTK_PRINT_OPERATION_ACTION_PREVIEW, GTK_WINDOW (document->window), NULL) == GTK_PRINT_OPERATION_RESULT_ERROR)
        warning_popup(document, _("Failed to preview page."));

    g_object_unref(print);
    g_object_unref(compositor);
}

void quit(struct Document * document) {
    GKeyFile * config = g_key_file_new();

    int width, height;
    gtk_window_get_size(document->window, &width, &height);
    g_key_file_set_integer(config, GROUP_KEY, "width", width);
    g_key_file_set_integer(config, GROUP_KEY, "height", height);

    g_key_file_set_string(config, GROUP_KEY, "font", document->font);

    g_key_file_set_boolean(config, GROUP_KEY, "wrap", document->wrap);
    g_key_file_set_boolean(config, GROUP_KEY, "syntax", document->syntax);

    char path[PATH_MAX];
    strcpy(path, (strlen(g_get_user_config_dir()) < PATH_MAX - strlen(CONFIG_FILE)) ? g_get_user_config_dir() : "~/.config");
    strcat(path, CONFIG_FILE);

    GError * error = NULL;
    char * data = g_key_file_to_data(config, NULL, &error);
    if (error) {
        puts(error->message);
        g_error_free(error);
        gtk_main_quit();
    }

    g_file_set_contents(path, data, -1, &error);
    if (error) {
        puts(error->message);
        g_error_free(error);
    }

    gtk_main_quit();
}

void exit_command(GtkWidget * self, struct Document * document) {
    
    if (gtk_text_buffer_get_modified(document->buffer) == FALSE) {
        quit(document);
        return;
    }

    GtkWidget * close = gtk_dialog_new_with_buttons("Janus", document->window, GTK_DIALOG_MODAL, "No", 0, "Cancel", 1, "Yes", 2, NULL);
    GtkWidget * content = gtk_dialog_get_content_area(GTK_DIALOG(close));
    GtkWidget * message = gtk_label_new(_("Would you like to save?"));

    gtk_container_add(GTK_CONTAINER(content), message);
    gtk_widget_show_all(content);

    int res = gtk_dialog_run (GTK_DIALOG (close));
    gtk_widget_destroy (close);

    switch (res) {
        case 0:
            quit(document);
            break;
        case 1:
            return;
        case 2:
            save_command(GTK_WIDGET(self), document);
            break;
    }
}

gboolean delete_event(GtkWidget* self, GdkEvent* event, struct Document * document) {
    exit_command(self, document);
    return TRUE;
}

void undo_command(GtkWidget * self, struct Document * document) {
    if (gtk_source_buffer_can_undo(GTK_SOURCE_BUFFER(document->buffer)))
        gtk_source_buffer_undo(GTK_SOURCE_BUFFER(document->buffer));
}

void redo_command(GtkWidget * self, struct Document * document) {
    if (gtk_source_buffer_can_redo(GTK_SOURCE_BUFFER(document->buffer)))
    gtk_source_buffer_redo(GTK_SOURCE_BUFFER(document->buffer));
}

void cut_command(GtkWidget * self, struct Document * document) {
    gtk_text_buffer_cut_clipboard(document->buffer, gtk_clipboard_get_default(gdk_display_get_default()), TRUE);
}

void copy_command(GtkWidget * self, struct Document * document) {
    gtk_text_buffer_copy_clipboard(document->buffer, gtk_clipboard_get_default(gdk_display_get_default()));
}

void paste_command(GtkWidget * self, struct Document * document) {
    gtk_text_buffer_paste_clipboard(document->buffer, gtk_clipboard_get_default(gdk_display_get_default()), NULL, TRUE);
}

void delete_command(GtkWidget * self, struct Document * document) {
    gtk_text_buffer_delete_selection(document->buffer, TRUE, TRUE);
}

void select_all_command(GtkWidget * self, struct Document * document) {

    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_start_iter(document->buffer, &start);
    gtk_text_buffer_get_end_iter(document->buffer, &end);

    gtk_text_buffer_select_range(document->buffer, &start, &end);
}

void search(struct Document * document) {

    GtkTextIter match_start;
    GtkTextIter match_end;

    if(gtk_source_search_context_forward(document->context, &document->last, &match_start, &match_end, NULL)) {
        gtk_text_buffer_select_range(document->buffer, &match_start, &match_end);
        document->last = match_end;

        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(document->view), &match_end, 0.1, FALSE, 0, 0);
    }
}

void search_entry(GtkWidget * entry, struct Document * document) {
    GtkSourceSearchSettings * settings = gtk_source_search_context_get_settings(document->context);
    gtk_source_search_settings_set_search_text(settings, gtk_entry_get_text(GTK_ENTRY(entry)));

    search(document);
}

void match_case(GtkWidget * self, GtkSourceSearchSettings * settings) {
    gtk_source_search_settings_set_case_sensitive(settings, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self)));
}

void search_command(GtkWidget * self, struct Document * document) {

    GtkTextIter last;
    gtk_text_buffer_get_start_iter(document->buffer, &last);
    document->last = last;

    GtkSourceSearchSettings * settings = gtk_source_search_context_get_settings(document->context);

    GtkWidget * dialog = gtk_dialog_new_with_buttons(_("Find"), document->window, GTK_DIALOG_DESTROY_WITH_PARENT, _("Cancel"), 0, _("Find"), 1, NULL);
    GtkWidget * content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget * box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget * entry = gtk_entry_new();
    g_signal_connect(entry, "activate", G_CALLBACK(search_entry), document);
    const gchar * text = gtk_source_search_settings_get_search_text(settings);
    if (text)
        gtk_entry_set_text(GTK_ENTRY(entry), text);

    GtkWidget * label = gtk_label_new(_("Find text:"));
    GtkWidget * bubble = gtk_check_button_new_with_label(_("Match case"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bubble), gtk_source_search_settings_get_case_sensitive(settings));
    g_signal_connect(bubble, "toggled", G_CALLBACK(match_case), settings);

    gtk_box_pack_start(GTK_BOX(box), label, 0, 0, 0);
    gtk_box_pack_end(GTK_BOX(box), entry, 0, 0, 0);

    gtk_container_add(GTK_CONTAINER(content), box);
    gtk_container_add(GTK_CONTAINER(content), bubble);
    gtk_widget_show_all(content);

    gtk_source_search_context_set_highlight(document->context, TRUE);
    int res = gtk_dialog_run (GTK_DIALOG (dialog));

    if (res == 1) 
        search_entry(entry, document);

    gtk_widget_destroy (dialog);
    gtk_source_search_context_set_highlight(document->context, FALSE);
}

void search_next_command(GtkWidget * self, struct Document * document) {
    search(document);
}

struct Replace {
    GtkSourceSearchSettings * settings;
    GtkWidget * search_entry;
    GtkWidget * replace_entry;
    _Bool all;
    struct Document * document;
};

void search_to_replace(GtkWidget * self, struct Replace * replace) {
    search_entry(replace->search_entry, replace->document);
}

void replace(GtkWidget * self, struct Replace * replace) {

    const char * text = gtk_entry_get_text(GTK_ENTRY(replace->replace_entry));
    gtk_source_search_settings_set_search_text(replace->settings, gtk_entry_get_text(GTK_ENTRY(replace->search_entry)));
    
    if (replace->all) {
        gtk_source_search_context_replace_all(replace->document->context, text, strlen(text), NULL);
        return;
    }

    GtkTextIter start;
    GtkTextIter end;

    if(!gtk_text_buffer_get_selection_bounds(replace->document->buffer, &start, &end)) {
        search_entry(replace->search_entry, replace->document);
        gtk_text_buffer_get_selection_bounds(replace->document->buffer, &start, &end);
    }
    gtk_source_search_context_replace(replace->document->context, &start, &end, text, strlen(text), NULL);
    replace->document->last = end;

}

void replace_all (GtkWidget * self, struct Replace * rep) {
    rep->all = !rep->all;
}

void replace_command(GtkWidget * self, struct Document * document) {
   
    GtkTextIter last;
    gtk_text_buffer_get_start_iter(document->buffer, &last);
    document->last = last;
    GtkSourceSearchSettings * settings = gtk_source_search_context_get_settings(document->context);

    GtkWidget * dialog = gtk_dialog_new_with_buttons(_("Replace"), document->window, GTK_DIALOG_DESTROY_WITH_PARENT, _("Cancel"), 0, NULL);
    GtkWidget * content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    // Search
    GtkWidget * box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget * label = gtk_label_new(_("Find text:"));
    GtkWidget * entry = gtk_entry_new();
    g_signal_connect(entry, "activate", G_CALLBACK(search_entry), document);
    const gchar * text = gtk_source_search_settings_get_search_text(settings);
    if (text)
        gtk_entry_set_text(GTK_ENTRY(entry), text);
    GtkWidget * search_button = gtk_button_new_with_label(_("Find"));

    gtk_box_pack_start(GTK_BOX(box), label, 0, 0, 0);
    gtk_box_pack_start(GTK_BOX(box), entry, 0, 0, 0);
    gtk_box_pack_start(GTK_BOX(box), search_button, 0, 0, 0);
    gtk_container_add(GTK_CONTAINER(content), box);

    // Replace
    GtkWidget * box2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget * label2 = gtk_label_new(_("Replace text"));
    GtkWidget * entry2 = gtk_entry_new();
    GtkWidget * replace_button = gtk_button_new_with_label(_("Replace"));

    struct Replace rep = {
        settings,
        entry,
        entry2,
        0,
        document
    };

    gtk_box_pack_start(GTK_BOX(box2), label2, 0, 0, 0);
    gtk_box_pack_start(GTK_BOX(box2), entry2, 0, 0, 0);
    g_signal_connect(entry2, "activate", G_CALLBACK(replace), &rep);
    gtk_box_pack_start(GTK_BOX(box2), replace_button, 0, 0, 0);
    gtk_container_add(GTK_CONTAINER(content), box2);

    GtkWidget * bubble = gtk_check_button_new_with_label(_("Match case"));
    GtkWidget * bubble2 = gtk_check_button_new_with_label(_("Replace all"));

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bubble), gtk_source_search_settings_get_case_sensitive(settings));
    g_signal_connect(bubble, "toggled", G_CALLBACK(match_case), settings);
    g_signal_connect(bubble2, "toggled", G_CALLBACK(replace_all), &rep);

    gtk_container_add(GTK_CONTAINER(content), bubble);
    gtk_container_add(GTK_CONTAINER(content), bubble2);

    g_signal_connect(search_button, "clicked", G_CALLBACK(search_to_replace), &rep);
    g_signal_connect(replace_button, "clicked", G_CALLBACK(replace), &rep);

    gtk_widget_show_all(content);

    gtk_source_search_context_set_highlight(document->context, TRUE);
    gtk_dialog_run (GTK_DIALOG (dialog));

    gtk_widget_destroy (dialog);
    gtk_source_search_context_set_highlight(document->context, FALSE);
}

int lines_in_buffer(GtkTextBuffer * buffer) {
    int count = 0;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);

    count = gtk_text_iter_get_line(&end) + 1;
    
    return count;
}

void go_to_command(GtkWidget * self, struct Document * document) {
    GtkWidget * dialog = gtk_dialog_new_with_buttons(_("Go To"), document->window, GTK_DIALOG_DESTROY_WITH_PARENT, _("Go To"), 0, _("Cancel"), 1, NULL);
    GtkWidget * content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget * line = gtk_label_new(_("Line number:"));
    int l = lines_in_buffer(document->buffer);

    GtkWidget * spin = gtk_spin_button_new_with_range(1, l, 1);
    
    gtk_container_add(GTK_CONTAINER(content), line);
    gtk_container_add(GTK_CONTAINER(content), spin);
    gtk_widget_show_all(content);

    int res = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (res == 0) {
        GtkTextIter jump;
        int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)) - 1;
        gtk_text_buffer_get_iter_at_line(document->buffer, &jump, value);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(document->view), &jump, 0.0, TRUE, 0.0, 0.15);
    }

    gtk_widget_destroy(dialog);

}

void set_font(struct Document * document) {
    PangoFontDescription * description = pango_font_description_from_string(document->font);
    if (description == NULL)
        return;
    PangoFontMask mask = pango_font_description_get_set_fields(description);

    char css[1024];
    strcpy(css, "textview {\n");

    if (mask & PANGO_FONT_MASK_STYLE) {
        strcat(css, "\tfont-style: ");
        switch (pango_font_description_get_style(description)) {
            case PANGO_STYLE_NORMAL:
                strcat(css, "normal");
                break;
            case PANGO_STYLE_OBLIQUE:
                strcat(css, "oblique");
                break;
            case PANGO_STYLE_ITALIC:
                strcat(css, "italic");
                break;
        }
        strcat(css, ";\n");
    }
    
    // only CSS2 values supported (For GTK3)
    if (mask & PANGO_FONT_MASK_VARIANT) {
        switch (pango_font_description_get_variant(description)) {
            case PANGO_VARIANT_NORMAL:
                strcat(css, "\tfont-variant: normal;\n");
                break;
            case PANGO_VARIANT_SMALL_CAPS:
                strcat(css, "\tfont-variant: small-caps;\n");
                break;
            default:
                break;
        }
    }

    if (mask & PANGO_FONT_MASK_WEIGHT) {
        strcat(css, "\tfont-weight: ");
        switch (pango_font_description_get_weight(description)) {
            case PANGO_WEIGHT_SEMILIGHT:
            case PANGO_WEIGHT_BOOK:
            case PANGO_WEIGHT_NORMAL:
                strcat(css, "normal");
                break;
            default:
                char newweight[4] = "000";
                newweight[0] = pango_font_description_get_weight(description)/100 + 0x30;
                strcat(css, newweight);
        }
        strcat(css, ";\n");
    }

    if (mask & PANGO_FONT_MASK_STRETCH) {
        strcat(css, "\tfont-stretch: ");
        switch (pango_font_description_get_stretch(description)) {
            case PANGO_STRETCH_ULTRA_CONDENSED:
                strcat(css, "ultra-condensed");
                break;
            case PANGO_STRETCH_EXTRA_CONDENSED:
                strcat(css, "extra-condensed");
                break;
            case PANGO_STRETCH_CONDENSED:
                strcat(css, "condensed");
                break;
            case PANGO_STRETCH_SEMI_CONDENSED:
                strcat(css, "semi-condensed");
                break;
            case PANGO_STRETCH_NORMAL:
                strcat(css, "normal");
                break;
            case PANGO_STRETCH_SEMI_EXPANDED:
                strcat(css, "semi-expanded");
                break;
            case PANGO_STRETCH_EXPANDED:
                strcat(css, "expanded");
                break;
            case PANGO_STRETCH_EXTRA_EXPANDED:
                strcat(css, "extra-expanded");
                break;
            case PANGO_STRETCH_ULTRA_EXPANDED:
                strcat(css, "ultra-expanded");
                break;
        }
        strcat(css, ";\n");
    }

    if (mask & PANGO_FONT_MASK_SIZE) {
        char size[64];
        sprintf(size, "\tfont-size: %dpt;\n", pango_font_description_get_size (description) / PANGO_SCALE);
        strcat(css, size);
    }

    if (mask & PANGO_FONT_MASK_FAMILY) {
        strcat(css, "\tfont-family: \"");
        g_strlcat(css, pango_font_description_get_family(description), 1024);
        g_strlcat(css, "\";\n", 1024);
    }

    g_strlcat(css, "}\n", 1024);

    GtkCssProvider * cssProvider = gtk_css_provider_new();
    GError * error = NULL;
    gtk_css_provider_load_from_data (cssProvider, css, -1, &error);
    if (error) {
        puts(error->message);
        g_error_free(error);
    }
    else {
        GtkStyleContext * context = gtk_widget_get_style_context(document->view);
        gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    g_object_unref(cssProvider);
    pango_font_description_free(description);
}

void font_selected(GtkDialog * dialog, int response_id, struct Document * document) {
    if (response_id != GTK_RESPONSE_OK) {
        return;
    }
    char * selected = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(dialog));
    free(document->font);
    document->font = selected;
    set_font(document);
}

void font_command(GtkWidget * self, struct Document * document) {
    GtkWidget * dialog = gtk_font_chooser_dialog_new(_("Fonts"), document->window);
    g_signal_connect(dialog, "response", G_CALLBACK(font_selected), document);

    gtk_font_chooser_set_font(GTK_FONT_CHOOSER(dialog), document->font);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void wrap_command(GtkWidget * self, struct Document * document) {
    document->wrap = !document->wrap;
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(document->view), document->wrap ? GTK_WRAP_WORD : GTK_WRAP_NONE);
}

void syntax_command(GtkWidget * self, struct Document * document) {
    document->syntax = !document->syntax;
    if (document->syntax) {
        GtkTextIter start, end;
        gtk_text_buffer_get_start_iter(document->buffer, &start);
        gtk_text_buffer_get_end_iter(document->buffer, &end);
        char * content = gtk_text_buffer_get_text(document->buffer, &start, &end, FALSE);
        char * content_type = g_content_type_guess(document->path, (const guchar *)content, strlen(content), NULL);
        free(content);
        GtkSourceLanguageManager * manager = gtk_source_language_manager_get_default();
        GtkSourceLanguage * language = gtk_source_language_manager_guess_language (manager, NULL, content_type);
        gtk_source_buffer_set_language(GTK_SOURCE_BUFFER(document->buffer), language);
        free(content_type);
    }
    else
        gtk_source_buffer_set_language(GTK_SOURCE_BUFFER(document->buffer), NULL);
}

void about_command(GtkWidget * self, struct Document * document) {
    GtkWidget * about_dialog = gtk_about_dialog_new();
    GtkAboutDialog * about = GTK_ABOUT_DIALOG(about_dialog);

    gtk_about_dialog_set_program_name(about, "Janus");
    gtk_about_dialog_set_logo_icon_name(about, "janus");

    const char * authors[] = {"Gabriel Holmann <gholmann@satyrn.net>", NULL};
    gtk_about_dialog_set_authors(about, authors);

    gtk_about_dialog_set_license_type(about, GTK_LICENSE_GPL_3_0);

    const char * comments = _("Janus is a simple Linux notepad intended to be small and efficient.");
    gtk_about_dialog_set_comments(about, comments);

    const char * website = "https://github.com/gholmann16/Janus";
    gtk_about_dialog_set_website(about, website);

    gtk_dialog_run(GTK_DIALOG(about_dialog));
    gtk_widget_destroy(about_dialog);
}
