#ifndef __SYSTEMUI_VTKLOCK_H_INCLUDED__
#define __SYSTEMUI_VTKLOCK_H_INCLUDED__

typedef struct {
  GtkWidget *time_label;
  GtkWidget *date_label;
} vtklockts;

typedef struct {
  guint count;
  guint hint;
} event_t;

typedef struct {
  GtkWidget *window;
  vtklockts ts;
  GtkWidget *slider;
  guint slider_status;
  gdouble slider_value;
  GtkAdjustment *slider_adjustment;
  DBusConnection *systemui_conn;
  int priority;
  guint update_timestamp_id;
  void(*unlock_handler)();
  event_t event[6];
  gulong slider_value_changed_id;
  gulong slider_change_value_id;
  gboolean dbus_filter_installed;
  time_t db_mtime;
} vtklock_t;

void visual_tklock_present_view(vtklock_t *vtklock);
vtklock_t *visual_tklock_new(DBusConnection *conn);
void visual_tklock_destroy_lock(vtklock_t *vtklock);
void visual_tklock_destroy(vtklock_t *vtklock);
void visual_tklock_set_unlock_handler(vtklock_t *vtklock, void (*handler)());
void visual_tklock_disable_lock(vtklock_t *vtklock);
void visual_tklock_create_view_whimsy(vtklock_t *vtklock);

#endif /* __SYSTEMUI_VTKLOCK_H_INCLUDED__ */
