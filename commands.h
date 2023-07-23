void open_command(GtkWidget * self, struct Document * document);

void new_command(void);

void save_command(GtkWidget * self, struct Document * document);

void save_as_command(GtkWidget * self, struct Document * document);

void exit_command(GtkWidget * self, struct Document * document);

gboolean delete_event(GtkWidget* self, GdkEvent* event, struct Document * document);

void undo_command(GtkWidget * self, struct Document * document);

void redo_command(GtkWidget * self, struct Document * document);