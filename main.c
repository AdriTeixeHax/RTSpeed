#include <adwaita.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CONFIG_DIR  "./config"
#define CONFIG_PATH "./config/config.json"
#define APP_ID      "com.rtspeed.configurator"

/* ── Constants ─────────────────────────────────────────────────── */

static const char * const GAIN_LABELS[15] = {
    "Iq P Gain",       "Iq I Gain",       "Iq D Gain",       "Iq HI",   "Iq LO",
    "Id P Gain",       "Id I Gain",       "Id D Gain",       "Id HI",   "Id LO",
    "Speed P Gain",    "Speed I Gain",    "Speed D Gain",    "Speed HI","Speed LO",
};
static const char * const GAIN_KEYS[15] = {
    "iq_p","iq_i","iq_d","iq_sat","iq_low",
    "id_p","id_i","id_d","id_sat","id_low",
    "speed_p","speed_i","speed_d","speed_sat","speed_low",
};
static const double GAIN_DEFAULTS[15] = { 1,0,0,100,-100, 1,0,0,100,-100, 1,0,0,100,-100 };

static const char * const FUZZY_PID_LABELS[10] = {
    "Base Kp", "Base Ki", "Base Kd",
    "Max dKp", "Max dKi", "Max dKd",
    "Speed Ref Max", "Speed Rate Max", "Output HI Limit", "Output LO Limit",
};
static const char * const FUZZY_PID_KEYS[10] = {
    "fuzzy_kp","fuzzy_ki","fuzzy_kd",
    "fuzzy_dkp","fuzzy_dki","fuzzy_dkd",
    "fuzzy_speed_max","fuzzy_rate_max","fuzzy_out_max","fuzzy_out_min",
};
static const double FUZZY_PID_DEFAULTS[10] = { 1,0,0, 0.5,0.5,0.5, 10000, 5000, 100,-100 };

/* ── Mini JSON ─────────────────────────────────────────────────── */

typedef enum  { JN_STR, JN_NUM, JN_OBJ } JNType;
typedef struct JNode JNode;
struct JNode {
    JNType      type;
    char       *str;
    double      num;
    GHashTable *obj;   /* char* → JNode*, owned */
};

static JNode *jn_new(JNType t)
{
    JNode *n = g_new0(JNode, 1);
    n->type = t;
    return n;
}
static void jn_free(JNode *n);
static GHashTable *jn_new_obj_table(void)
{
    return g_hash_table_new_full(g_str_hash, g_str_equal,
                                 g_free, (GDestroyNotify)jn_free);
}
static void
jn_free(JNode *n)
{
    if (!n) return;
    g_free(n->str);
    if (n->obj) g_hash_table_destroy(n->obj);
    g_free(n);
}

static JNode *
jn_dup(const JNode *n)
{
    if (!n) return NULL;
    JNode *c = jn_new(n->type);
    c->str = g_strdup(n->str);
    c->num = n->num;
    if (n->obj) {
        c->obj = jn_new_obj_table();
        GHashTableIter it; gpointer k, v;
        g_hash_table_iter_init(&it, n->obj);
        while (g_hash_table_iter_next(&it, &k, &v))
            g_hash_table_insert(c->obj, g_strdup((char*)k), jn_dup((JNode*)v));
    }
    return c;
}

/* Parser */
static void   skip_ws(const char **s) { while (**s && g_ascii_isspace(**s)) (*s)++; }
static JNode *parse_value(const char **s);

static char *
parse_string(const char **s)
{
    if (**s != '"') return NULL;
    (*s)++;
    GString *out = g_string_new(NULL);
    while (**s && **s != '"') {
        if (**s == '\\') {
            (*s)++;
            switch (**s) {
                case '"':  g_string_append_c(out, '"');  break;
                case '\\': g_string_append_c(out, '\\'); break;
                case 'n':  g_string_append_c(out, '\n'); break;
                case 't':  g_string_append_c(out, '\t'); break;
                default:   g_string_append_c(out, **s);  break;
            }
        } else {
            g_string_append_c(out, **s);
        }
        (*s)++;
    }
    if (**s == '"') (*s)++;
    return g_string_free(out, FALSE);
}

static JNode *
parse_object(const char **s)
{
    if (**s != '{') return NULL;
    (*s)++;
    JNode *n = jn_new(JN_OBJ);
    n->obj = jn_new_obj_table();
    skip_ws(s);
    if (**s == '}') { (*s)++; return n; }
    while (**s) {
        skip_ws(s);
        char *key = parse_string(s);
        if (!key) break;
        skip_ws(s);
        if (**s == ':') (*s)++;
        skip_ws(s);
        JNode *val = parse_value(s);
        if (val) g_hash_table_insert(n->obj, key, val);
        else     g_free(key);
        skip_ws(s);
        if      (**s == ',') { (*s)++; }
        else if (**s == '}') { (*s)++; break; }
        else break;
    }
    return n;
}

static JNode *
parse_value(const char **s)
{
    skip_ws(s);
    if (**s == '"') {
        JNode *n = jn_new(JN_STR);
        n->str = parse_string(s);
        return n;
    }
    if (**s == '{') return parse_object(s);
    if (**s == '-' || g_ascii_isdigit(**s)) {
        char *end;
        double d = g_ascii_strtod(*s, &end);
        if (end != *s) { *s = end; JNode *n = jn_new(JN_NUM); n->num = d; return n; }
    }
    return NULL;
}

static JNode *jn_parse(const char *t) { return t ? parse_value(&t) : NULL; }

/* Accessors */
static const char *
jn_str(const JNode *root, const char *key, const char *def)
{
    if (!root || !root->obj) return def;
    JNode *n = g_hash_table_lookup(root->obj, key);
    return (n && n->type == JN_STR && n->str) ? n->str : def;
}
static double
jn_num(const JNode *root, const char *key, double def)
{
    if (!root || !root->obj) return def;
    JNode *n = g_hash_table_lookup(root->obj, key);
    return (n && n->type == JN_NUM) ? n->num : def;
}
static JNode *
jn_obj(const JNode *root, const char *key)
{
    if (!root || !root->obj) return NULL;
    JNode *n = g_hash_table_lookup(root->obj, key);
    return (n && n->type == JN_OBJ) ? n : NULL;
}

/* Writer helpers */
static void
gs_append_str_escaped(GString *out, const char *s)
{
    g_string_append_c(out, '"');
    for (; s && *s; s++) {
        if      (*s == '"')  g_string_append(out, "\\\"");
        else if (*s == '\\') g_string_append(out, "\\\\");
        else if (*s == '\n') g_string_append(out, "\\n");
        else if (*s == '\t') g_string_append(out, "\\t");
        else                 g_string_append_c(out, *s);
    }
    g_string_append_c(out, '"');
}

/* ── App struct ─────────────────────────────────────────────────── */

typedef struct _App App;
struct _App {
    AdwApplication       *adw_app;
    AdwApplicationWindow *window;

    char   *project_dir;
    char   *build_cmd;
    char   *flash_cmd;
    char   *stlink_dev;
    char   *exe_path;
    double  gains[15];
    double  fuzzy_pid[10];
    JNode  *presets;           /* JN_OBJ: name → JN_OBJ of gains */

    AdwActionRow  *row_proj;
    AdwEntryRow   *row_build;
    AdwEntryRow   *row_flash;
    AdwEntryRow   *row_stlink;
    GtkSpinButton *spins[25];
    GtkDropDown   *combo_presets;
    GtkStringList *preset_model;
    GtkEntry      *entry_preset_name;

    GtkButton         *btn_flash;
    GtkTextBuffer     *log_buf;
    GtkScrolledWindow *log_scroll;
};

/* ── Config ─────────────────────────────────────────────────────── */

static void
config_load(App *a)
{
    a->project_dir = g_strdup("");
    a->build_cmd   = g_strdup("make -C Debug");
    a->flash_cmd   = g_strdup("pkexec st-flash --reset write Debug/STM32F411CEU6-Test.elf 0x8000000");
    a->stlink_dev  = g_strdup("");
    for (int i = 0; i < 15; i++) a->gains[i] = GAIN_DEFAULTS[i];
    for (int i = 0; i < 10;  i++) a->fuzzy_pid[i] = FUZZY_PID_DEFAULTS[i];
    a->presets = jn_new(JN_OBJ);
    a->presets->obj = jn_new_obj_table();

    char *content = NULL;
    if (!g_file_get_contents(CONFIG_PATH, &content, NULL, NULL)) return;

    JNode *root = jn_parse(content);
    g_free(content);
    if (!root) return;

    g_free(a->project_dir); a->project_dir = g_strdup(jn_str(root, "project_dir", ""));
    g_free(a->build_cmd);   a->build_cmd   = g_strdup(jn_str(root, "build_cmd", "make -C Debug"));
    g_free(a->flash_cmd);   a->flash_cmd   = g_strdup(jn_str(root, "flash_cmd",
                                              "pkexec st-flash --reset write Debug/STM32F411CEU6-Test.elf 0x8000000"));
    g_free(a->stlink_dev);  a->stlink_dev  = g_strdup(jn_str(root, "stlink_dev", ""));

    JNode *gains = jn_obj(root, "gains");
    for (int i = 0; i < 15; i++)
        a->gains[i] = jn_num(gains, GAIN_KEYS[i], GAIN_DEFAULTS[i]);

    JNode *fuzzy = jn_obj(root, "fuzzy_pid");
    for (int i = 0; i < 9; i++)
        a->fuzzy_pid[i] = jn_num(fuzzy, FUZZY_PID_KEYS[i], FUZZY_PID_DEFAULTS[i]);

    JNode *presets = jn_obj(root, "presets");
    if (presets) {
        jn_free(a->presets);
        a->presets = jn_dup(presets);
    }

    jn_free(root);
}

static void
config_save(App *a)
{
    GString *out = g_string_new("{\n");

    g_string_append(out, "    \"project_dir\": ");
    gs_append_str_escaped(out, a->project_dir ? a->project_dir : "");
    g_string_append(out, ",\n");

    g_string_append(out, "    \"build_cmd\": ");
    gs_append_str_escaped(out, gtk_editable_get_text(GTK_EDITABLE(a->row_build)));
    g_string_append(out, ",\n");

    g_string_append(out, "    \"flash_cmd\": ");
    gs_append_str_escaped(out, gtk_editable_get_text(GTK_EDITABLE(a->row_flash)));
    g_string_append(out, ",\n");

    g_string_append(out, "    \"stlink_dev\": ");
    gs_append_str_escaped(out, gtk_editable_get_text(GTK_EDITABLE(a->row_stlink)));
    g_string_append(out, ",\n");

    g_string_append(out, "    \"gains\": {\n");
    for (int i = 0; i < 15; i++) {
        g_string_append_printf(out, "        \"%s\": %g%s\n",
            GAIN_KEYS[i], gtk_spin_button_get_value(a->spins[i]),
            i < 14 ? "," : "");
    }
    g_string_append(out, "    },\n");

    g_string_append(out, "    \"fuzzy_pid\": {\n");
    for (int i = 0; i < 10; i++) {
        g_string_append_printf(out, "        \"%s\": %g%s\n",
            FUZZY_PID_KEYS[i], gtk_spin_button_get_value(a->spins[15 + i]),
            i < 9 ? "," : "");
    }
    g_string_append(out, "    },\n");

    g_string_append(out, "    \"presets\": {");
    if (a->presets && a->presets->obj &&
        g_hash_table_size(a->presets->obj) > 0) {
        g_string_append(out, "\n");
        GList *keys = g_list_sort(g_hash_table_get_keys(a->presets->obj),
                                  (GCompareFunc)strcmp);
        guint total = g_list_length(keys), idx = 0;
        for (GList *k = keys; k; k = k->next, idx++) {
            const char *pname = k->data;
            g_string_append(out, "        ");
            gs_append_str_escaped(out, pname);
            g_string_append(out, ": {\n");
            JNode *pnode = g_hash_table_lookup(a->presets->obj, pname);
            for (int i = 0; i < 15; i++) {
                double v = jn_num(pnode, GAIN_KEYS[i], 0.0);
                g_string_append_printf(out, "            \"%s\": %g%s\n",
                    GAIN_KEYS[i], v, i < 14 ? "," : "");
            }
            for (int i = 0; i < 9; i++) {
                double v = jn_num(pnode, FUZZY_PID_KEYS[i], 0.0);
                g_string_append_printf(out, "            \"%s\": %g%s\n",
                    FUZZY_PID_KEYS[i], v, i < 8 ? "," : "");
            }
            g_string_append_printf(out, "        }%s\n", idx < total - 1 ? "," : "");
        }
        g_list_free(keys);
    } else {
        g_string_append(out, "\n");
    }
    g_string_append(out, "    }\n}\n");

    g_mkdir_with_parents(CONFIG_DIR, 0755);
    GError *err = NULL;
    g_file_set_contents(CONFIG_PATH, out->str, out->len, &err);
    if (err) { g_warning("config save: %s", err->message); g_clear_error(&err); }
    g_string_free(out, TRUE);
}

/* ── Log helpers (thread-safe) ──────────────────────────────────── */

typedef struct { App *app; char *text; } LogMsg;

static gboolean
log_idle(gpointer data)
{
    LogMsg *m = data;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(m->app->log_buf, &end);
    gtk_text_buffer_insert(m->app->log_buf, &end, m->text, -1);
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(m->app->log_scroll);
    gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
    g_free(m->text); g_free(m);
    return G_SOURCE_REMOVE;
}
static void
log_append(App *a, const char *text)
{
    LogMsg *m = g_new(LogMsg, 1);
    m->app = a; m->text = g_strdup(text);
    g_idle_add(log_idle, m);
}

/* ── Flash thread ───────────────────────────────────────────────── */

typedef struct {
    App   *app;
    char  *project_dir;
    char  *build_cmd;
    char  *flash_cmd;
    double gains[15];
    double fuzzy_pid[10];
    gboolean run_build;
    gboolean run_flash;
} FlashData;

static gboolean
flash_done_idle(gpointer data)
{
    gtk_widget_set_sensitive(GTK_WIDGET(((App *)data)->btn_flash), TRUE);
    return G_SOURCE_REMOVE;
}

static void on_sudo_dialog_response(AdwDialog *dialog, char *resp, gpointer user_data);

static gpointer flash_thread(gpointer data);

static gboolean
run_cmd_in_dir(App *a, const char *cwd, const char *cmd)
{
    char msg[1024];
    g_snprintf(msg, sizeof(msg), "Running in: %s\n", cwd);
    log_append(a, msg);
    g_snprintf(msg, sizeof(msg), "$ %s\n", cmd);
    log_append(a, msg);

    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, cwd);

    GError             *err  = NULL;
    const gchar * const argv[] = { "sh", "-c", cmd, NULL };
    GSubprocess        *proc = g_subprocess_launcher_spawnv(launcher, argv, &err);
    g_object_unref(launcher);

    if (!proc) {
        g_snprintf(msg, sizeof(msg), "Error: %s\n", err ? err->message : "unknown");
        log_append(a, msg);
        g_clear_error(&err);
        return FALSE;
    }

    GInputStream     *out = g_subprocess_get_stdout_pipe(proc);
    GDataInputStream *dis = g_data_input_stream_new(out);
    char *line;
    while ((line = g_data_input_stream_read_line(dis, NULL, NULL, NULL)) != NULL) {
        char *nl = g_strconcat(line, "\n", NULL);
        log_append(a, nl);
        g_free(nl); g_free(line);
    }
    g_object_unref(dis);

    gboolean ok = g_subprocess_wait_check(proc, NULL, &err);
    if (!ok) {
        g_snprintf(msg, sizeof(msg), "Failed: %s\n", err ? err->message : "exit error");
        log_append(a, msg);
        g_clear_error(&err);
    }
    g_object_unref(proc);
    return ok;
}

static gpointer
flash_thread(gpointer data)
{
    FlashData *fd = data;
    App       *a  = fd->app;
    char       msg[512];

    log_append(a, "Generating pid_gains.h...\n");

    char *inc = g_build_filename(fd->project_dir, "Core", "Inc", NULL);
    if (!g_file_test(inc, G_FILE_TEST_IS_DIR)) {
        g_free(inc);
        inc = g_build_filename(fd->project_dir, "Inc", NULL);
    }
    g_mkdir_with_parents(inc, 0755);
    char *hpath = g_build_filename(inc, "pid_gains.h", NULL);
    g_free(inc);

     FILE *f = fopen(hpath, "w");
     if (!f) {
         g_snprintf(msg, sizeof(msg), "Error: cannot write %s\nPermission denied.\n", hpath);
         log_append(a, msg);

         AdwDialog *dialog = ADW_DIALOG(adw_message_dialog_new(
             GTK_WINDOW(a->window),
             "Elevated Privileges Required",
             "Cannot write to the file. Do you want to retry with sudo?"));
         adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dialog), "cancel", "Cancel");
         adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dialog), "sudo", "Use Sudo");
         adw_message_dialog_set_response_appearance(
             ADW_MESSAGE_DIALOG(dialog), "sudo", ADW_RESPONSE_DESTRUCTIVE);

FlashData *fddup = g_new0(FlashData, 1);
          fddup->app         = a;
          fddup->project_dir = g_strdup(fd->project_dir);
          fddup->build_cmd   = g_strdup(fd->build_cmd);
          fddup->flash_cmd   = g_strdup(fd->flash_cmd);
          fddup->run_build   = fd->run_build;
          fddup->run_flash  = fd->run_flash;
          for (int i = 0; i < 15; i++) fddup->gains[i] = fd->gains[i];
          for (int i = 0; i < 10;  i++) fddup->fuzzy_pid[i] = fd->fuzzy_pid[i];
          g_object_set_data_full(G_OBJECT(dialog), "relaunch-data", fddup, g_free);
         g_object_set_data(G_OBJECT(dialog), "app-pointer", a);

         g_signal_connect(dialog, "response", G_CALLBACK(on_sudo_dialog_response), NULL);
         gtk_window_present(GTK_WINDOW(dialog));

         g_free(hpath);
         goto done;
     }
      fprintf(f, "#ifndef PID_GAINS_H\n#define PID_GAINS_H\n\n");
    fprintf(f, "/* Auto-generated by RTSpeed Configurator */\n\n");
    fprintf(f, "/* Iq */\n");
    fprintf(f, "#define IQ_KP  %.2ff\n#define IQ_KI  %.2ff\n#define IQ_KD  %.2ff\n#define IQ_HI  %.2ff\n#define IQ_LO  %.2ff\n\n",
        fd->gains[0], fd->gains[1], fd->gains[2], fd->gains[3], fd->gains[4]);
    fprintf(f, "/* Id */\n");
    fprintf(f, "#define ID_KP  %.2ff\n#define ID_KI  %.2ff\n#define ID_KD  %.2ff\n#define ID_HI  %.2ff\n#define ID_LO  %.2ff\n\n",
        fd->gains[5], fd->gains[6], fd->gains[7], fd->gains[8], fd->gains[9]);
    fprintf(f, "/* Speed */\n");
    fprintf(f, "#define SPEED_KP %.2ff\n#define SPEED_KI %.2ff\n#define SPEED_KD %.2ff\n#define SPEED_HI %.2ff\n#define SPEED_LO %.2ff\n\n",
        fd->gains[10], fd->gains[11], fd->gains[12], fd->gains[13], fd->gains[14]);
    fprintf(f, "/* Fuzzy PID */\n");
    fprintf(f, "#define FUZZY_KP   %.2ff\n#define FUZZY_KI   %.2ff\n#define FUZZY_KD   %.2ff\n",
        fd->fuzzy_pid[0], fd->fuzzy_pid[1], fd->fuzzy_pid[2]);
    fprintf(f, "#define FUZZY_DKP  %.2ff\n#define FUZZY_DKI  %.2ff\n#define FUZZY_DKD  %.2ff\n",
        fd->fuzzy_pid[3], fd->fuzzy_pid[4], fd->fuzzy_pid[5]);
    fprintf(f, "#define FUZZY_SPEED_MAX %.2ff\n", fd->fuzzy_pid[6]);
    fprintf(f, "#define FUZZY_RATE_MAX %.2ff\n", fd->fuzzy_pid[7]);
    fprintf(f, "#define FUZZY_OUT_MAX %.2ff\n#define FUZZY_OUT_MIN %.2ff\n\n",
        fd->fuzzy_pid[8], fd->fuzzy_pid[9]);
    fprintf(f, "#endif /* PID_GAINS_H */\n");
    fclose(f);

     g_snprintf(msg, sizeof(msg), "Wrote gains to %s\n", hpath);
     log_append(a, msg);
     g_free(hpath);

     if (fd->run_build && fd->build_cmd && fd->build_cmd[0]) {
         log_append(a, "Building...\n");
         if (!run_cmd_in_dir(a, fd->project_dir, fd->build_cmd)) {
             log_append(a, "Build failed.\n");
             goto done;
         }
         log_append(a, "Build successful.\n");
     }

if (fd->run_flash && fd->flash_cmd && fd->flash_cmd[0]) {
          log_append(a, "Flashing...\n");

          char test_cmd[256];
          g_snprintf(test_cmd, sizeof(test_cmd), "st-flash --version 2>&1 | head -1");
          if (!run_cmd_in_dir(a, fd->project_dir, test_cmd)) {
              log_append(a, "\n*** ERROR: Cannot access ST-Link device ***\n\n");
              log_append(a, "Permission denied. To fix this, you have two options:\n\n");
              log_append(a, "Option 1 - Add udev rule (permanent):\n");
              log_append(a, "  echo 'ATTR{idVendor}==\"0483\", ATTR{idProduct}==\"3748\", MODE=\"0666\"' | sudo tee /etc/udev/rules.d/99-stlink.rules\n");
              log_append(a, "  sudo udevadm reload\n");
              log_append(a, "  Then unplug and replug your ST-Link.\n\n");
              log_append(a, "Option 2 - Use pkexec (each time):\n");
              log_append(a, "  In Configure page, prepend 'pkexec ' to Flash Command.\n");
              log_append(a, "  Example: pkexec st-flash --reset write Debug/STM32F411CEU6-Test.elf 0x8000000\n\n");
              goto done;
          }

          if (!run_cmd_in_dir(a, fd->project_dir, fd->flash_cmd)) {
              log_append(a, "Flash failed.\n");
              goto done;
          }
          log_append(a, "Flash successful.\n");
      }

     log_append(a, "Done!\n");

done:
     g_free(fd->project_dir);
     g_free(fd->build_cmd);
     g_free(fd->flash_cmd);
     g_free(fd);
     g_idle_add(flash_done_idle, a);
     return NULL;
}

static void
on_sudo_dialog_response(AdwDialog *dialog, char *resp, gpointer user_data)
{
    (void)user_data;
    FlashData *fd = g_object_get_data(G_OBJECT(dialog), "relaunch-data");
    App *app = g_object_get_data(G_OBJECT(dialog), "app-pointer");
    if (g_strcmp0(resp, "sudo") == 0 && fd) {
        gtk_window_destroy(GTK_WINDOW(dialog));

        gtk_widget_set_sensitive(GTK_WIDGET(app->btn_flash), FALSE);
        gtk_text_buffer_set_text(app->log_buf, "", -1);
        log_append(app, "Running with elevated privileges...\n");

        char *inc = g_build_filename(fd->project_dir, "Core", "Inc", NULL);
        if (!g_file_test(inc, G_FILE_TEST_IS_DIR)) {
            g_free(inc);
            inc = g_build_filename(fd->project_dir, "Inc", NULL);
        }
        char *hpath = g_build_filename(inc, "pid_gains.h", NULL);
        g_free(inc);

        GString *content = g_string_new(NULL);
        g_string_append(content, "#ifndef PID_GAINS_H\n#define PID_GAINS_H\n\n");
        g_string_append(content, "/* Auto-generated by RTSpeed Configurator */\n\n");
        g_string_append(content, "/* Iq */\n");
        g_string_append_printf(content, "#define IQ_KP  %.2ff\n#define IQ_KI  %.2ff\n#define IQ_KD  %.2ff\n#define IQ_HI  %.2ff\n#define IQ_LO  %.2ff\n\n",
            fd->gains[0], fd->gains[1], fd->gains[2], fd->gains[3], fd->gains[4]);
        g_string_append(content, "/* Id */\n");
        g_string_append_printf(content, "#define ID_KP  %.2ff\n#define ID_KI  %.2ff\n#define ID_KD  %.2ff\n#define ID_HI  %.2ff\n#define ID_LO  %.2ff\n\n",
            fd->gains[5], fd->gains[6], fd->gains[7], fd->gains[8], fd->gains[9]);
        g_string_append(content, "/* Speed */\n");
        g_string_append_printf(content, "#define SPEED_KP %.2ff\n#define SPEED_KI %.2ff\n#define SPEED_KD %.2ff\n#define SPEED_HI %.2ff\n#define SPEED_LO %.2ff\n\n",
            fd->gains[10], fd->gains[11], fd->gains[12], fd->gains[13], fd->gains[14]);
        g_string_append(content, "/* Fuzzy PID */\n");
        g_string_append_printf(content, "#define FUZZY_KP   %.2ff\n#define FUZZY_KI   %.2ff\n#define FUZZY_KD   %.2ff\n",
            fd->fuzzy_pid[0], fd->fuzzy_pid[1], fd->fuzzy_pid[2]);
        g_string_append_printf(content, "#define FUZZY_DKP  %.2ff\n#define FUZZY_DKI  %.2ff\n#define FUZZY_DKD  %.2ff\n",
            fd->fuzzy_pid[3], fd->fuzzy_pid[4], fd->fuzzy_pid[5]);
        g_string_append_printf(content, "#define FUZZY_SPEED_MAX %.2ff\n", fd->fuzzy_pid[6]);
        g_string_append_printf(content, "#define FUZZY_RATE_MAX %.2ff\n", fd->fuzzy_pid[7]);
        g_string_append_printf(content, "#define FUZZY_OUT_MAX %.2ff\n#define FUZZY_OUT_MIN %.2ff\n\n",
            fd->fuzzy_pid[8], fd->fuzzy_pid[9]);
        g_string_append(content, "#endif /* PID_GAINS_H */\n");

        char cmd[16384];
        g_snprintf(cmd, sizeof(cmd), "pkexec tee %s << 'ENDOFFILE'\n%sENDOFFILE", hpath, content->str);
        g_string_free(content, TRUE);
        g_free(hpath);

        if (run_cmd_in_dir(app, fd->project_dir, cmd)) {
            log_append(app, "Wrote pid_gains.h with sudo\n");

            if (fd->run_build && fd->build_cmd && fd->build_cmd[0]) {
                log_append(app, "Building...\n");
                if (!run_cmd_in_dir(app, fd->project_dir, fd->build_cmd)) {
                    log_append(app, "Build failed.\n");
                    gtk_widget_set_sensitive(GTK_WIDGET(app->btn_flash), TRUE);
                    g_free(fd->project_dir);
                    g_free(fd->build_cmd);
                    g_free(fd->flash_cmd);
                    g_free(fd);
                    gtk_window_destroy(GTK_WINDOW(dialog));
                    return;
                }
                log_append(app, "Build successful.\n");
            }

            if (fd->run_flash && fd->flash_cmd && fd->flash_cmd[0]) {
                log_append(app, "Flashing...\n");
                if (!run_cmd_in_dir(app, fd->project_dir, fd->flash_cmd)) {
                    log_append(app, "Flash failed.\n");
                    gtk_widget_set_sensitive(GTK_WIDGET(app->btn_flash), TRUE);
                    g_free(fd->project_dir);
                    g_free(fd->build_cmd);
                    g_free(fd->flash_cmd);
                    g_free(fd);
                    gtk_window_destroy(GTK_WINDOW(dialog));
                    return;
                }
                log_append(app, "Flash successful.\n");
            }

            log_append(app, "Done!\n");
        } else {
            log_append(app, "Failed to write file\n");
        }

        gtk_widget_set_sensitive(GTK_WIDGET(app->btn_flash), TRUE);

        g_free(fd->project_dir);
        g_free(fd->build_cmd);
        g_free(fd->flash_cmd);
        g_free(fd);
    } else if (fd) {
        g_free(fd->project_dir);
        g_free(fd->build_cmd);
        g_free(fd->flash_cmd);
        g_free(fd);
        if (app) gtk_widget_set_sensitive(GTK_WIDGET(app->btn_flash), TRUE);
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

/* ── Configure page callbacks ───────────────────────────────────── */

static void
on_folder_selected(GObject *src, GAsyncResult *res, gpointer user_data)
{
    App           *a   = user_data;
    GError        *err = NULL;
    GFile         *file = gtk_file_dialog_select_folder_finish(
                              GTK_FILE_DIALOG(src), res, &err);
    if (!file) { g_clear_error(&err); return; }
    g_free(a->project_dir);
    a->project_dir = g_file_get_path(file);
    adw_action_row_set_subtitle(a->row_proj,
        a->project_dir[0] ? a->project_dir : "Not selected");
    g_object_unref(file);
    config_save(a);
}

static void
on_select_folder(GtkButton *btn, gpointer user_data)
{
    App           *a      = user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select STM32CubeIDE Project Folder");
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(a->window),
                                  NULL, on_folder_selected, a);
    g_object_unref(dialog);
}

static void
on_save_config(GtkButton *btn, gpointer user_data) { config_save(user_data); }

static void
on_load_preset(GtkButton *btn, gpointer user_data)
{
    App             *a    = user_data;
    GtkStringObject *item = GTK_STRING_OBJECT(
        gtk_drop_down_get_selected_item(a->combo_presets));
    if (!item) return;
    const char *name = gtk_string_object_get_string(item);
     JNode *pnode = g_hash_table_lookup(a->presets->obj, name);
     if (!pnode) return;
     for (int i = 0; i < 15; i++)
         gtk_spin_button_set_value(a->spins[i], jn_num(pnode, GAIN_KEYS[i], GAIN_DEFAULTS[i]));
     for (int i = 0; i < 9; i++)
         gtk_spin_button_set_value(a->spins[15 + i], jn_num(pnode, FUZZY_PID_KEYS[i], FUZZY_PID_DEFAULTS[i]));
}

static void
on_save_preset(GtkButton *btn, gpointer user_data)
{
    App        *a    = user_data;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(a->entry_preset_name));
    if (!name || !name[0]) return;

     JNode *pnode = jn_new(JN_OBJ);
     pnode->obj = jn_new_obj_table();
     for (int i = 0; i < 15; i++) {
         JNode *vn = jn_new(JN_NUM);
         vn->num = gtk_spin_button_get_value(a->spins[i]);
         g_hash_table_insert(pnode->obj, g_strdup(GAIN_KEYS[i]), vn);
     }
     for (int i = 0; i < 9; i++) {
         JNode *vn = jn_new(JN_NUM);
         vn->num = gtk_spin_button_get_value(a->spins[15 + i]);
         g_hash_table_insert(pnode->obj, g_strdup(FUZZY_PID_KEYS[i]), vn);
     }

     gboolean is_new = !g_hash_table_contains(a->presets->obj, name);
    g_hash_table_insert(a->presets->obj, g_strdup(name), pnode);

    if (is_new) gtk_string_list_append(a->preset_model, name);

    guint n = g_list_model_get_n_items(G_LIST_MODEL(a->preset_model));
    for (guint i = 0; i < n; i++) {
        if (strcmp(gtk_string_list_get_string(a->preset_model, i), name) == 0) {
            gtk_drop_down_set_selected(a->combo_presets, i);
            break;
        }
    }
    gtk_editable_set_text(GTK_EDITABLE(a->entry_preset_name), "");
    config_save(a);
}

static void
on_delete_preset(GtkButton *btn, gpointer user_data)
{
    App             *a    = user_data;
    GtkStringObject *item = GTK_STRING_OBJECT(
        gtk_drop_down_get_selected_item(a->combo_presets));
    if (!item) return;
    const char *name = gtk_string_object_get_string(item);
    if (!g_hash_table_remove(a->presets->obj, name)) return;
    guint n = g_list_model_get_n_items(G_LIST_MODEL(a->preset_model));
    for (guint i = 0; i < n; i++) {
        if (strcmp(gtk_string_list_get_string(a->preset_model, i), name) == 0) {
            gtk_string_list_remove(a->preset_model, i);
            break;
        }
    }
    config_save(a);
}

/* ── Copy log callback ─────────────────────────────────────────── */

static void
on_copy_log(GtkButton *btn, gpointer user_data)
{
    App *a = user_data;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(a->log_buf, &start, &end);
    char *text = gtk_text_buffer_get_text(a->log_buf, &start, &end, FALSE);
    GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(a->window));
    gdk_clipboard_set_text(clipboard, text);
    g_free(text);
}

/* ── Flash callback ─────────────────────────────────────────────── */

static void
on_flash_clicked(GtkButton *btn, gpointer user_data)
{
    App *a = user_data;
    if (!a->project_dir || !a->project_dir[0]) {
        log_append(a, "Error: select a project directory in Configure first.\n");
        return;
    }
    if (gtk_widget_get_sensitive(GTK_WIDGET(a->btn_flash)) == FALSE) {
        return;
    }
    gtk_widget_set_sensitive(GTK_WIDGET(a->btn_flash), FALSE);
    gtk_text_buffer_set_text(a->log_buf, "", -1);

    FlashData *fd   = g_new0(FlashData, 1);
    fd->app         = a;
    fd->project_dir = g_strdup(a->project_dir);
    fd->build_cmd   = g_strdup(gtk_editable_get_text(GTK_EDITABLE(a->row_build)));
    fd->flash_cmd   = g_strdup(gtk_editable_get_text(GTK_EDITABLE(a->row_flash)));
    fd->run_build   = TRUE;
    fd->run_flash  = TRUE;
    for (int i = 0; i < 15; i++) fd->gains[i] = gtk_spin_button_get_value(a->spins[i]);
    for (int i = 0; i < 10;  i++) fd->fuzzy_pid[i] = gtk_spin_button_get_value(a->spins[15 + i]);

    g_thread_new("flash", flash_thread, fd);
}

/* ── Page builders ──────────────────────────────────────────────── */

static void
setup_configure_page(App *a, AdwViewStack *stack)
{
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());

    /* Project group */
    AdwPreferencesGroup *grp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp, "STM32CubeIDE Project");

    a->row_proj = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(a->row_proj), "Project Path");
    adw_action_row_set_subtitle(a->row_proj,
        a->project_dir[0] ? a->project_dir : "Not selected");
    GtkButton *b_folder = GTK_BUTTON(gtk_button_new_with_label("Select Folder"));
    gtk_widget_set_valign(GTK_WIDGET(b_folder), GTK_ALIGN_CENTER);
    g_signal_connect(b_folder, "clicked", G_CALLBACK(on_select_folder), a);
    adw_action_row_add_suffix(a->row_proj, GTK_WIDGET(b_folder));
    adw_preferences_group_add(grp, GTK_WIDGET(a->row_proj));

    a->row_build = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(a->row_build), "Build Command");
    gtk_editable_set_text(GTK_EDITABLE(a->row_build), a->build_cmd);
    adw_preferences_group_add(grp, GTK_WIDGET(a->row_build));

    a->row_flash = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(a->row_flash), "Flash Command");
    gtk_editable_set_text(GTK_EDITABLE(a->row_flash), a->flash_cmd);
    adw_preferences_group_add(grp, GTK_WIDGET(a->row_flash));

    a->row_stlink = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(a->row_stlink), "ST-Link Device (optional)");
    gtk_editable_set_text(GTK_EDITABLE(a->row_stlink), a->stlink_dev ? a->stlink_dev : "");
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->row_stlink), "/dev/bus/usb/003/027");
    adw_preferences_group_add(grp, GTK_WIDGET(a->row_stlink));

    GtkButton *b_save = GTK_BUTTON(gtk_button_new_with_label("Save Configuration"));
    gtk_widget_add_css_class(GTK_WIDGET(b_save), "suggested-action");
    gtk_widget_add_css_class(GTK_WIDGET(b_save), "pill");
    gtk_widget_set_halign(GTK_WIDGET(b_save), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(GTK_WIDGET(b_save), 8);
    g_signal_connect(b_save, "clicked", G_CALLBACK(on_save_config), a);
    adw_preferences_group_add(grp, GTK_WIDGET(b_save));
    adw_preferences_page_add(page, grp);

    AdwPreferencesGroup *presets_grp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(presets_grp, "Presets");

    a->preset_model  = gtk_string_list_new(NULL);
    a->combo_presets = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(a->preset_model), NULL));
    gtk_widget_set_valign(GTK_WIDGET(a->combo_presets), GTK_ALIGN_CENTER);

    if (a->presets && a->presets->obj) {
        GList *keys = g_list_sort(g_hash_table_get_keys(a->presets->obj),
                                  (GCompareFunc)strcmp);
        for (GList *k = keys; k; k = k->next)
            gtk_string_list_append(a->preset_model, (char *)k->data);
        g_list_free(keys);
    }

    AdwActionRow *row_manage = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_manage), "Preset Management");
    adw_action_row_add_suffix(row_manage, GTK_WIDGET(a->combo_presets));

    GtkButton *b_load = GTK_BUTTON(gtk_button_new_with_label("Load"));
    gtk_widget_set_valign(GTK_WIDGET(b_load), GTK_ALIGN_CENTER);
    g_signal_connect(b_load, "clicked", G_CALLBACK(on_load_preset), a);
    adw_action_row_add_suffix(row_manage, GTK_WIDGET(b_load));

    GtkButton *b_del = GTK_BUTTON(gtk_button_new_with_label("Delete"));
    gtk_widget_add_css_class(GTK_WIDGET(b_del), "destructive-action");
    gtk_widget_set_valign(GTK_WIDGET(b_del), GTK_ALIGN_CENTER);
    g_signal_connect(b_del, "clicked", G_CALLBACK(on_delete_preset), a);
    adw_action_row_add_suffix(row_manage, GTK_WIDGET(b_del));
    adw_preferences_group_add(presets_grp, GTK_WIDGET(row_manage));

    AdwActionRow *row_savep = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_savep), "Save Current As...");
    a->entry_preset_name = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->entry_preset_name, "Preset Name");
    gtk_widget_set_valign(GTK_WIDGET(a->entry_preset_name), GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(row_savep, GTK_WIDGET(a->entry_preset_name));

    GtkButton *b_savep = GTK_BUTTON(gtk_button_new_with_label("Save"));
    gtk_widget_add_css_class(GTK_WIDGET(b_savep), "suggested-action");
    gtk_widget_set_valign(GTK_WIDGET(b_savep), GTK_ALIGN_CENTER);
    g_signal_connect(b_savep, "clicked", G_CALLBACK(on_save_preset), a);
    adw_action_row_add_suffix(row_savep, GTK_WIDGET(b_savep));
    adw_preferences_group_add(presets_grp, GTK_WIDGET(row_savep));
    adw_preferences_page_add(page, presets_grp);

    AdwPreferencesGroup *ctrl_grp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(ctrl_grp, "Controller Parameters");

    static const char *ctrl_names[4] = {
        "Current (Iq)", "Current (Id)", "Speed", "Fuzzy PID",
    };
    int ctrl_count = 4;

    GtkBox *ctrl_grid = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 12));
    gtk_widget_set_hexpand(GTK_WIDGET(ctrl_grid), TRUE);

    for (int c = 0; c < ctrl_count; c++) {
        AdwPreferencesGroup *g2 = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
        adw_preferences_group_set_title(g2, ctrl_names[c]);
        gtk_widget_set_hexpand(GTK_WIDGET(g2), TRUE);
        gtk_box_append(ctrl_grid, GTK_WIDGET(g2));

        int rows = (c == 3) ? 10 : 5;
        int base_idx = (c == 3) ? 15 : (c * 5);

        for (int k = 0; k < rows; k++) {
            int idx = base_idx + k;
            double min_val, max_val, step_val;
            const char *label;

            if (c == 3) {
                if (k == 6) {
                    min_val = 0.0; max_val = 50000.0; step_val = 0.01;
                } else if (k == 7) {
                    min_val = 0.0; max_val = 50000.0; step_val = 0.01;
                } else if (k == 8 || k == 9) {
                    min_val = -100000.0; max_val = 100000.0; step_val = 0.01;
                } else {
                    min_val = 0.0; max_val = 10000.0; step_val = 0.01;
                }
                label = FUZZY_PID_LABELS[k];
            } else {
                gboolean is_hi = (k == 3);
                gboolean is_lo = (k == 4);
                if (is_hi) {
                    min_val = 0.0; max_val = 1000000.0; step_val = 0.01;
                } else if (is_lo) {
                    min_val = -1000000.0; max_val = 1000000.0; step_val = 0.01;
                } else {
                    min_val = 0.0; max_val = 10000.0; step_val = 0.01;
                }
                label = GAIN_LABELS[idx];
            }

            GtkSpinButton *spin = GTK_SPIN_BUTTON(
                gtk_spin_button_new_with_range(min_val, max_val, step_val));
            gtk_spin_button_set_digits(spin, 2);
            gtk_spin_button_set_value(spin, (c == 3) ? a->fuzzy_pid[k] : a->gains[idx]);
            gtk_widget_set_valign(GTK_WIDGET(spin), GTK_ALIGN_CENTER);
            gtk_widget_set_size_request(GTK_WIDGET(spin), 180, -1);
            GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_NONE);
            gtk_widget_add_controller(GTK_WIDGET(spin), scroll);
            a->spins[idx] = spin;
            AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), label);
            adw_action_row_add_suffix(row, GTK_WIDGET(spin));
            adw_preferences_group_add(g2, GTK_WIDGET(row));
        }
    }

    adw_preferences_group_add(ctrl_grp, GTK_WIDGET(ctrl_grid));
    adw_preferences_page_add(page, ctrl_grp);

    adw_view_stack_add_titled_with_icon(stack, GTK_WIDGET(page),
        "configure", "Configure", "preferences-system-symbolic");
}

static void
setup_flash_page(App *a, AdwViewStack *stack)
{
    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 12));
    gtk_widget_set_margin_top(GTK_WIDGET(box), 24);
    gtk_widget_set_margin_bottom(GTK_WIDGET(box), 24);
    gtk_widget_set_margin_start(GTK_WIDGET(box), 24);
    gtk_widget_set_margin_end(GTK_WIDGET(box), 24);

    AdwStatusPage *status = ADW_STATUS_PAGE(adw_status_page_new());
    adw_status_page_set_title(status, "Build & Flash");
    adw_status_page_set_description(status,
        "Write pid_gains.h, build project, and flash to device");
    adw_status_page_set_icon_name(status, "drive-harddisk-symbolic");
    gtk_box_append(box, GTK_WIDGET(status));

    a->btn_flash = GTK_BUTTON(gtk_button_new_with_label("Build & Flash"));
    gtk_widget_add_css_class(GTK_WIDGET(a->btn_flash), "suggested-action");
    gtk_widget_add_css_class(GTK_WIDGET(a->btn_flash), "pill");
    gtk_widget_set_halign(GTK_WIDGET(a->btn_flash), GTK_ALIGN_CENTER);
    g_signal_connect(a->btn_flash, "clicked", G_CALLBACK(on_flash_clicked), a);
    gtk_box_append(box, GTK_WIDGET(a->btn_flash));

    a->log_buf = gtk_text_buffer_new(NULL);
    GtkWidget *tv = gtk_text_view_new_with_buffer(a->log_buf);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_widget_set_margin_top(tv, 2);
    gtk_widget_set_margin_bottom(tv, 2);
    gtk_widget_set_margin_start(tv, 2);
    gtk_widget_set_margin_end(tv, 2);

    a->log_scroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
    gtk_scrolled_window_set_child(a->log_scroll, tv);
    gtk_widget_set_vexpand(GTK_WIDGET(a->log_scroll), TRUE);
    gtk_widget_set_margin_top(GTK_WIDGET(a->log_scroll), 2);
    gtk_widget_set_margin_bottom(GTK_WIDGET(a->log_scroll), 2);
    gtk_widget_set_margin_start(GTK_WIDGET(a->log_scroll), 2);
    gtk_widget_set_margin_end(GTK_WIDGET(a->log_scroll), 2);

    GtkFrame *frame = GTK_FRAME(gtk_frame_new(NULL));
    gtk_frame_set_child(frame, GTK_WIDGET(a->log_scroll));
    gtk_widget_set_vexpand(GTK_WIDGET(frame), TRUE);
    gtk_box_append(box, GTK_WIDGET(frame));

    GtkButton *btn_copy = GTK_BUTTON(gtk_button_new_with_label("Copy Log"));
    gtk_widget_set_halign(GTK_WIDGET(btn_copy), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(GTK_WIDGET(btn_copy), 8);
    g_signal_connect(btn_copy, "clicked", G_CALLBACK(on_copy_log), a);
    gtk_box_append(box, GTK_WIDGET(btn_copy));

    adw_view_stack_add_titled_with_icon(stack, GTK_WIDGET(box),
        "flash", "Flash", "media-flash-symbolic");
}

static void
setup_about_page(App *a, AdwViewStack *stack)
{
    (void)a;
    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 12));

    AdwStatusPage *status = ADW_STATUS_PAGE(adw_status_page_new());
    adw_status_page_set_title(status, "RTSpeed Configurator");
    adw_status_page_set_description(status,
        "Custom inverter configuration and flashing tool.");
    adw_status_page_set_icon_name(status, "help-about-symbolic");
    gtk_box_append(box, GTK_WIDGET(status));

    GtkLabel *lbl = GTK_LABEL(gtk_label_new(
        "1. Configure PID gains for the inverter's controllers\n"
        "2. Recompile the STM32 firmware\n"
        "3. Flash it to the board using ST-Link v2\n\n"
        "Built with C, GTK4 and Libadwaita."));
    gtk_label_set_justify(lbl, GTK_JUSTIFY_CENTER);
    gtk_box_append(box, GTK_WIDGET(lbl));

    adw_view_stack_add_titled_with_icon(stack, GTK_WIDGET(box),
        "about", "About", "help-about-symbolic");
}

/* ── Activate ───────────────────────────────────────────────────── */

static void
on_activate(GApplication *gapp, gpointer user_data)
{
    App *a = user_data;

    a->window = ADW_APPLICATION_WINDOW(
        adw_application_window_new(GTK_APPLICATION(gapp)));
    gtk_window_set_title(GTK_WINDOW(a->window), "RTSpeed Configurator");
    gtk_window_set_default_size(GTK_WINDOW(a->window), 800, 600);

    AdwToolbarView *toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_application_window_set_content(a->window, GTK_WIDGET(toolbar));

    AdwHeaderBar *header = ADW_HEADER_BAR(adw_header_bar_new());
    adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));

    AdwViewStack *stack = ADW_VIEW_STACK(adw_view_stack_new());
    adw_toolbar_view_set_content(toolbar, GTK_WIDGET(stack));

    AdwViewSwitcher *switcher = ADW_VIEW_SWITCHER(adw_view_switcher_new());
    adw_view_switcher_set_stack(switcher, stack);
    adw_view_switcher_set_policy(switcher, ADW_VIEW_SWITCHER_POLICY_WIDE);
    adw_header_bar_set_title_widget(header, GTK_WIDGET(switcher));

    setup_configure_page(a, stack);
    setup_flash_page(a, stack);
    setup_about_page(a, stack);

    gtk_window_present(GTK_WINDOW(a->window));
}

/* ── main ───────────────────────────────────────────────────────── */

int
main(int argc, char *argv[])
{
    g_setenv("GDK_GL", "disable", TRUE);

    App a = {0};
    config_load(&a);

    a.adw_app = adw_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(a.adw_app, "activate", G_CALLBACK(on_activate), &a);

    int status = g_application_run(G_APPLICATION(a.adw_app), argc, argv);

    g_object_unref(a.adw_app);
    g_free(a.project_dir);
    g_free(a.build_cmd);
    g_free(a.flash_cmd);
    jn_free(a.presets);

    return status;
}
