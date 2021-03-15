#include <gtkmm.h>
#include <fstream>

extern "C" {
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4panel/libxfce4panel.h>
}

namespace Xfce {
    class Rc {
        private:
            XfceRc *raw;

        public:
            Rc(const char *path, bool readonly): raw(xfce_rc_simple_open(path, readonly)) { }
            ~Rc() { if (raw) xfce_rc_close(raw); }
            operator bool() const { return raw; }

            template <typename T> void write(const char* key, const T &val) const;
            template <typename T> T read(const char* key, T val) const;
            void set_group(const std::string& name) const { xfce_rc_set_group(raw, name.c_str()); }
            bool has_group(const std::string& name) const { return xfce_rc_has_group(raw, name.c_str()); }
    };

    template <> void Rc::write(const char* key, const int &val)           const { xfce_rc_write_int_entry (raw, key, val); }
    template <> void Rc::write(const char* key, const bool &val)          const { xfce_rc_write_bool_entry(raw, key, val); }
    template <> void Rc::write(const char* key, const Glib::ustring &val) const { xfce_rc_write_entry     (raw, key, val.c_str()); }
    template <> void Rc::write(const char* key, const std::string &val)   const { xfce_rc_write_entry     (raw, key, val.c_str()); }

    template <> int         Rc::read(const char* key, int val)           const { return xfce_rc_read_int_entry  (raw, key, val); };
    template <> bool        Rc::read(const char* key, bool val)          const { return xfce_rc_read_bool_entry (raw, key, val); };
    template <> const char* Rc::read(const char* key, const char* val)   const { return xfce_rc_read_entry      (raw, key, val); };
}

constexpr int UPDATE_INTERVAL = 2000;
constexpr int PBAR_THICKNESS = 10;
constexpr int BORDER = 4;
constexpr int WIDGET_SPACING = 2;

class Alarm {
    public:
        Glib::ustring name, info;
        std::string command;
        int time;
        bool is_recurring, is_auto_start, timer_on;

        bool is_repeating; /* True while alarm repeats */
        bool is_paused; /* True if the countdown is paused */
        bool is_countdown; /* True if the alarm type is contdown */
        int timeout_period_in_sec,    /* Active countdown period */
            rem_repetitions;      /* Remaining repeats */
        sigc::connection timeout,repeat_timeout;	/* The timeout IDs */
        std::unique_ptr<Glib::Timer> timer; /* Keeps track of the time elapsed */

        static std::unique_ptr<Alarm> from_xfce_rc(const Xfce::Rc &rc) {
            auto alrm = std::make_unique<Alarm>();
            alrm->name          = rc.read("timername",    "No name");
            alrm->command       = rc.read("timercommand", "");
            alrm->info          = rc.read("timerinfo",    "");
            alrm->is_countdown  = rc.read("is_countdown", true);
            alrm->is_recurring  = rc.read("is_recur",     false);
            alrm->is_auto_start = rc.read("autostart",    false);
            alrm->time          = rc.read("time",         0);
            return std::move(alrm);
        }

        void to_xfce_rc(const Xfce::Rc &rc) const {
            rc.write("timername",    name);
            rc.write("time",         time);
            rc.write("timercommand", command);
            rc.write("timerinfo",    info);
            rc.write("is_countdown", is_countdown);
            rc.write("is_recur",     is_recurring);
            rc.write("autostart",    is_auto_start);
        }
};

class Plugin {
    private:
        XfcePanelPlugin *plugin = nullptr;

        std::unique_ptr<Gtk::Box> box;
        std::unique_ptr<Gtk::ProgressBar> pbar;
        std::unique_ptr<Gtk::Button> buttonadd;
        std::unique_ptr<Gtk::Button> buttonedit;
        std::unique_ptr<Gtk::Button> buttonremove;
        std::unique_ptr<Gtk::Button> buttonup;
        std::unique_ptr<Gtk::Button> buttondown;
        std::unique_ptr<Gtk::Box> global_command_box;
        std::unique_ptr<Gtk::Entry> glob_command_entry;
        std::unique_ptr<Gtk::Box> repeat_alarm_box;
        std::unique_ptr<Gtk::SpinButton> spin_repeat;
        std::unique_ptr<Gtk::SpinButton> spin_interval;
        std::unique_ptr<Gtk::TreeView> tree;
        std::unique_ptr<Gtk::Menu> menu;
        Glib::RefPtr<Gtk::ListStore> liststore;

        bool nowin_if_alarm;
        bool use_global_command;
        Glib::ustring global_command;
        bool repeat_alarm_command;
        int repetitions = 1;
        int repeat_interval = 10;

        std::vector<std::unique_ptr<Alarm>> alarm_list;
        // Alarm *selected = nullptr;

        void Plugin::set_row(Gtk::TreeModel::iterator iter, Alarm *alrm);
        void add_alarm(std::unique_ptr<Alarm> newalarm);
        Gtk::TreeModel::iterator Plugin::selected_row() { return tree->get_selection()->get_selected(); }
        void Plugin::update_selected() { set_row(selected_row(), selected_row()->get_value(columns.alrm)); }
        int Plugin::get_alarm_index(Alarm *alrm) {
            return std::find_if(alarm_list.begin(), alarm_list.end(), [=](const auto &p){ return p.get()==alrm; }) - alarm_list.begin();
        }

        void fill_liststore() {
            liststore->clear();
            for (const auto &alrm: alarm_list)
                set_row(liststore->append(), alrm.get());
        }


    public:
        Plugin(const Plugin&) = delete;
        Plugin(XfcePanelPlugin *plugin);

        void show_about();
        bool size_changed(int size);
        void update_pbar_orientation();
        void create_options();
        void load_settings();
        void save_settings();

        void options_dialog_response(Gtk::Dialog*, int respone_id);
        void remove_clicked();
        void up_clicked();
        void down_clicked();
        void tree_selected(bool selected) {
            for (auto *btn: {buttonedit.get(), buttonremove.get(), buttonup.get(), buttondown.get()})
                btn->set_sensitive(selected);
        }

        bool pbar_clicked(GdkEventButton*);
        void start_stop_callback(Alarm*);
        void start_timer(Alarm*);
        bool update_function();
        bool repeat_alarm(Alarm *alrm);
        void pause_resume_selected(Alarm*);

        class Columns: public Gtk::TreeModel::ColumnRecord {
            public:
                Gtk::TreeModelColumn<Alarm*> alrm;
                Gtk::TreeModelColumn<Glib::ustring> name;
                Gtk::TreeModelColumn<Glib::ustring> info;
                Gtk::TreeModelColumn<std::string> command;
                Columns() { add(alrm); add(name); add(info); add(command); }
        };

        Columns columns;
};

extern "C" {
    XFCE_PANEL_PLUGIN_REGISTER([](XfcePanelPlugin *plugin) { new Plugin(plugin); });
    static void wrap_show_about(XfcePanelPlugin*, Plugin *thiz) { thiz->show_about(); }
    static gboolean wrap_size_changed(XfcePanelPlugin*, gint size, Plugin *thiz) { return thiz->size_changed(size); }
    static void wrap_orient_change(XfcePanelPlugin*, GtkOrientation, Plugin *thiz) { thiz->update_pbar_orientation(); }
    static void wrap_create_options(XfcePanelPlugin*, Plugin *thiz) { return thiz->create_options(); }
    static void wrap_plugin_free(XfcePanelPlugin*, Plugin *thiz) { delete thiz; Gtk::Main::quit(); }
    static void wrap_save_settings(XfcePanelPlugin*, Plugin *thiz) { thiz->save_settings(); }
    static gboolean wrap_pbar_clicked(XfcePanelPlugin*, GdkEventButton* btn, Plugin *thiz) { return thiz->pbar_clicked(btn); }
}

bool Plugin::size_changed(int size)
{
    if (xfce_panel_plugin_get_orientation (plugin) == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_set_size_request(GTK_WIDGET(plugin), -1, size);
    else
        gtk_widget_set_size_request(GTK_WIDGET(plugin), size, -1);

    return true;
}

Plugin::Plugin(XfcePanelPlugin *plugin_): plugin(plugin_) {
    Gtk::Main::init_gtkmm_internals();  // for Glib::wrap

    xfce_textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");


    pbar = std::make_unique<Gtk::ProgressBar>();
    liststore = Gtk::ListStore::create(columns);
    box = std::make_unique<Gtk::Box>();

    gtk_widget_set_tooltip_text(GTK_WIDGET(plugin), "");

    load_settings();

    //Check if an alarm is auto start to start it at creation
    for (const auto &alrm: alarm_list) {
        if (alrm->is_auto_start){
            start_timer(pd,alrm);
        }
    }

    box->set_border_width(BORDER / 2);
    gtk_container_add (GTK_CONTAINER (plugin), GTK_WIDGET(box->gobj()));

    pbar->set_fraction(0.1);
    box->pack_start(*pbar, false, false, 0);

    update_pbar_orientation();

    g_signal_connect(plugin, "button_press_event",  G_CALLBACK (::wrap_pbar_clicked),   this);

    gtk_widget_show_all (GTK_WIDGET (plugin));

    g_signal_connect(plugin, "free-data",           G_CALLBACK (::wrap_plugin_free),    this);
    g_signal_connect(plugin, "save",                G_CALLBACK (::wrap_save_settings),  this);
    g_signal_connect(plugin, "orientation-changed", G_CALLBACK (::wrap_orient_change),  this);
    g_signal_connect(plugin, "size-changed",        G_CALLBACK (::wrap_size_changed),   this);

    xfce_panel_plugin_menu_show_configure (plugin);
    g_signal_connect(plugin, "configure-plugin",    G_CALLBACK (::wrap_create_options), this);

    xfce_panel_plugin_menu_show_about (plugin);
    g_signal_connect(plugin, "about",               G_CALLBACK (::wrap_show_about),     this);
}

void Plugin::update_pbar_orientation()
{
    if (xfce_panel_plugin_get_orientation(plugin) == GTK_ORIENTATION_HORIZONTAL)
    {
        /* vertical bar */
        box->set_orientation(Gtk::ORIENTATION_HORIZONTAL);
        pbar->set_orientation(Gtk::ORIENTATION_VERTICAL);

        pbar->set_inverted(true);
        pbar->set_halign(Gtk::ALIGN_CENTER);
        pbar->set_hexpand(true);

        gtk_widget_set_size_request(GTK_WIDGET(plugin), -1, xfce_panel_plugin_get_size(plugin));
    }
    else
    {
        /* horizontal bar */
        box->set_orientation(Gtk::ORIENTATION_VERTICAL);
        pbar->set_orientation(Gtk::ORIENTATION_HORIZONTAL);

        pbar->set_inverted(false);
        pbar->set_halign(Gtk::ALIGN_CENTER);
        pbar->set_hexpand(false);

        gtk_widget_set_size_request(GTK_WIDGET(plugin), xfce_panel_plugin_get_size(plugin), -1);
    }
}


void Plugin::show_about() {
    const std::vector<Glib::ustring> author{ "Kemal Ilgar Eroğlu <ilgar_eroglu@yahoo.com>"};
    const Glib::ustring translators (
            "Mohammad Alhargan <malham1@hotmail.com> \n"
            "Marcos Antonio Alvarez Costales <marcoscostales@gmail.com> \n"
            "Harald Servat <redcrash@gmail.com> \n"
            "Michal Várady <miko.vaji@gmail.com>\n"
            "Per Kongstad <p_kongstad@op.pl>\n"
            "Simon Schneider <simon@schneiderimtal.de>\n"
            "Efstathios Iosifidis <iosifidis@opensuse.org>\n"
            "Jeff Bailes <thepizzaking@gmail.com>\n"
            "Sergio <oigres200@gmail.com>\n"
            "Piarres Beobide <pi@beobide.net>\n"
            "Maximilian Schleiss <maximilian@xfce.org>\n"
            "Leandro Regueiro <leandro.regueiro@gmail.com>\n"
            "Ivica Kolić <ikoli@yahoo.com>\n"
            "Gabor Kelemen <kelemeng at gnome dot hu>\n"
            "Andhika Padmawan <andhika.padmawan@gmail.com>\n"
            "Cristian Marchi <cri.penta@gmail.com>\n"
            "Nobuhiro Iwamatsu <iwamatsu@nigauri.org>\n"
            "Seong-ho Cho <darkcircle.0426@gmail.com>\n"
            "Rihards Priedītis <rprieditis@inbox.lv>\n"
            "Pjotr <pjotrvertaalt@gmail.com>\n"
            "Piotr Sokół <piotr.sokol@10g.pl>\n"
            "Sérgio Marques <smarquespt@gmail.com>\n"
            "Rafael Ferreira <rafael.f.f1@gmail.com>\n"
            "Dima Smirnov <arch@cnc-parts.info>\n"
            "Tomáš Vadina <kyberdev@gmail.com>\n"
            "Besnik Bleta <besnik@programeshqip.org>\n"
            "Саша Петровић <salepetronije@gmail.com>\n"
            "Daniel Nylander <po@danielnylander.se>\n"
            "Kemal Ilgar Eroğlu <ilgar_eroglu@yahoo.com>\n"
            "Gheyret T.Kenji <gheyret@gmail.com>\n"
            "Dmitry Nikitin <luckas_fb@mail.ru>\n"
            "Muhammad Ali Makki <makki.ma@gmail.com>\n"
            "Hunt Xu <huntxu@live.cn>\n"
            "Cheng-Chia Tseng <pswo10680@gmail.com>\n"
            );

    auto icon = Glib::wrap(xfce_panel_pixbuf_from_source ("xfce4-timer-plugin", NULL, 48));
    auto about_dialog = Gtk::AboutDialog();
    about_dialog.set_title(_("About xfce4-timer-plugin"));
    about_dialog.set_logo(icon);
    about_dialog.set_license(xfce_get_license_text (XFCE_LICENSE_TEXT_GPL));
    about_dialog.set_version(PACKAGE_VERSION);
    about_dialog.set_program_name(PACKAGE_NAME);
    about_dialog.set_comments(_("A plugin to define countdown timers or alarms at given times."));
    about_dialog.set_website("https://docs.xfce.org/panel-plugins/xfce4-timer-plugin");
    about_dialog.set_copyright(_("Copyright (c) 2005-2018"));
    about_dialog.set_authors(author);
    about_dialog.set_translator_credits(translators);
    about_dialog.run();
}

class SpinButtonInt: public Gtk::SpinButton {
    public:
        SpinButtonInt(int min, int max) {
            set_range(min, max);
            set_increments(1, 10);
        }
};

class AddEditDialog: Gtk::Dialog {
    std::unique_ptr<Gtk::SpinButton> timeh, times, timem; /* Spinbuttons for h-m-s format */
    std::unique_ptr<Gtk::SpinButton> time_h, time_m; /* Spinbuttons for 24h format */
    std::unique_ptr<Gtk::Entry> name, command; /* Name, and command entries */
    std::unique_ptr<Gtk::RadioButton> rb1; /* Radio button for the h-m-s format */
    std::unique_ptr<Gtk::CheckButton> recur_cb, autostart_cb; /* check buttons for recurring alarm, autostart */

    void alarmdialog_countdown_toggled(const Gtk::RadioButton &rb) {
        bool active = rb.get_active();
        timeh->set_sensitive(active);
        timem->set_sensitive(active);
        times->set_sensitive(active);
    }

    void alarmdialog_alarmtime_toggled(const Gtk::RadioButton &rb){
        bool active = rb.get_active();
        time_h->set_sensitive(active);
        time_m->set_sensitive(active);
    }

    void ok_add() {
        newalarm = std::make_unique<Alarm>();
        // selected = newalarm.get();
        ok_edit(newalarm.get());
    }

    void ok_edit(Alarm *alrm) {
        alrm->name = name->get_text(); // g_strdup (gtk_entry_get_text (GTK_ENTRY (adata->name)));
        alrm->command = command->get_text(); // g_strdup (gtk_entry_get_text (GTK_ENTRY (adata->command)));
        alrm->is_countdown = rb1->get_active(); // gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (adata->rb1));
        alrm->is_recurring = recur_cb->get_active(); // gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(adata->recur_cb));
        alrm->is_auto_start = autostart_cb->get_active(); // gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(adata->autostart_cb));


        if (rb1->get_active()) {
            int t1 = timeh->get_value_as_int();
            int t2 = timem->get_value_as_int();
            int t3 = times->get_value_as_int();
            alrm->time = t1 * 3600 + t2 * 60 + t3;

            if (t1 > 0)
                alrm->info = Glib::ustring::sprintf(_("%dh %dm %ds"), t1, t2, t3);
            else if (t2 > 0)
                alrm->info = Glib::ustring::sprintf(_("%dm %ds"), t2, t3);
            else
                alrm->info = Glib::ustring::sprintf(_("%ds"), t3);
        } else {
            /* The 24h format (alarm at specified time). Save time in minutes */
            int t1 = time_h->get_value_as_int();
            int t2 = time_m->get_value_as_int();
            alrm->time = t1 * 60 + t2;
            alrm->info = Glib::ustring::sprintf(_("At %02d:%02d"), t1, t2);
        }
    }

    public:
    std::unique_ptr<Alarm> newalarm;

    AddEditDialog(Gtk::Window& toplevel, Alarm *alrm): Gtk::Dialog() {
        /* Set it modal and transient for main window. */
        set_modal(true);
        set_transient_for(toplevel);

        set_icon_name("xfce4-timer-plugin");

        /* Set title */
        // add(*Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6));

        auto box = get_content_area();
        box->set_border_width(2);
        box->set_spacing(6);

        auto vbox = Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6);
        box->pack_start(vbox, false, false, 0);
        vbox.set_border_width(12);

        /***********/
        Gtk::Box *hbox;
        hbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
        vbox.pack_start(*hbox, false, false, 0);

        name = std::make_unique<Gtk::Entry>();

        hbox->pack_start(*Gtk::make_managed<Gtk::Label>(_("Name:")), false, false, 0);
        hbox->pack_start(*name, true, true, 0);

        vbox.pack_start(*Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_HORIZONTAL), false, false, 6);

        /**********/
        rb1 = std::make_unique<Gtk::RadioButton>("Enter the countdown time");
        rb1->signal_toggled().connect([&](){ alarmdialog_countdown_toggled(*rb1); });
        auto rb_group = rb1->get_group();
        auto rb2 = Gtk::RadioButton(rb_group, _("Enter the time of alarm (24h format)"));
        rb2.signal_toggled().connect([&](){ alarmdialog_countdown_toggled(rb2); });

        vbox.pack_start(*rb1, true, true, 0);

        hbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        vbox.pack_start(*hbox, true, true, 0);
        hbox->set_margin_start(12);

        timeh = std::make_unique<SpinButtonInt>(0, 23);
        hbox->pack_start(*timeh, false, false, 0);
        hbox->pack_start(*Gtk::make_managed<Gtk::Label>(_("h ")), false, false, 0);

        timem = std::make_unique<SpinButtonInt>(0, 59);
        hbox->pack_start(*timem, false, false, 0);
        hbox->pack_start(*Gtk::make_managed<Gtk::Label>(_("m ")), false, false, 0);

        times = std::make_unique<SpinButtonInt>(0, 59);
        hbox->pack_start(*times, false, false, 0);
        hbox->pack_start(*Gtk::make_managed<Gtk::Label>(_("s ")), false, false, 0);

        hbox->pack_start(*Gtk::make_managed<Gtk::Label>(_("or ")), false, false, 0);
        vbox.pack_start(rb2, true, true, 0);

        hbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        vbox.pack_start(*hbox, true, true, 0);
        hbox->set_margin_start(12);

        time_h = std::make_unique<SpinButtonInt>(0, 23);
        hbox->pack_start(*time_h, false, false, 0);
        hbox->pack_start(*Gtk::make_managed<Gtk::Label>(_(":")), false, false, 0);
        time_m = std::make_unique<SpinButtonInt>(0, 59);
        hbox->pack_start(*time_m, false, false, 0);

        vbox.pack_start(*Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_HORIZONTAL), false, false, 6);

        /****************/

        hbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
        vbox.pack_start(*hbox, true, true, 0);
        hbox->pack_start(*Gtk::make_managed<Gtk::Label>(_("Command to run:")), false, false, 0);

        command = std::make_unique<Gtk::Entry>();
        hbox->pack_start(*command, true, true, 0);

        /****************/

        vbox.pack_start(*Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_HORIZONTAL), false, false, 6);

        //add recurring alarm check button
        recur_cb = std::make_unique<Gtk::CheckButton>(_("Recurring alarm"));
        recur_cb->set_active(false);
        vbox.pack_start(*recur_cb, false, false, 0);

        //add alarm autostart check button
        autostart_cb = std::make_unique<Gtk::CheckButton>(_("Auto start when plugin loads"));
        autostart_cb->set_active(false);
        vbox.pack_start(*autostart_cb, false, false, 0);

        auto bbox = Gtk::ButtonBox(Gtk::ORIENTATION_HORIZONTAL);
        bbox.set_spacing(6);
        bbox.set_layout(Gtk::BUTTONBOX_END);
        box->pack_start(*hbox, true, true, 0);

        auto button_cancel = Gtk::Button(_("Cancel"));
        hbox->pack_start(button_cancel, true, true, 0);
        button_cancel.signal_clicked().connect([&](){ close(); });

        auto button_accept = Gtk::Button(_("Accept"));
        hbox->pack_start(button_accept, true, true, 0);
        button_accept.signal_clicked().connect([&](){ !alrm ?ok_add() :ok_edit(alrm); close(); });

        /* If this is the add window, we're done */
        if (!alrm)
        {
            set_title(_("Add new alarm"));
            alarmdialog_alarmtime_toggled(rb2);
            show_all();
            run();
            return;
        }

        /* Else fill the values in the boxes with the current choices */
        name->set_text(alrm->name);
        command->set_text(alrm->command);

        //load settings
        recur_cb->set_active(alrm->is_recurring);
        autostart_cb->set_active(alrm->is_auto_start);

        int time = alrm->time;

        if (alrm->is_countdown)
        {
            timeh->set_value(time / 3600);
            timem->set_value((time % 3600) / 60);
            times->set_value(time % 60);
            alarmdialog_alarmtime_toggled(rb2);
            rb1->set_active(true);
        }
        else
        {
            time_h->set_value(time / 60);
            time_m->set_value(time % 60);
            rb2.set_active(true); // active by default
        }

        set_title(_("Edit alarm"));
        show_all();
        run();
    }
};

void Plugin::add_alarm(std::unique_ptr<Alarm> alrm) {
    if (!alrm)
        return;

    set_row(liststore->append(), alrm.get());
    alarm_list.push_back(std::move(alrm));
}

void Plugin::set_row(Gtk::TreeModel::iterator iter, Alarm *alrm) {
    iter->set_value(columns.alrm,    alrm);
    iter->set_value(columns.name,    alrm->name);
    iter->set_value(columns.info,    alrm->info);
    iter->set_value(columns.command, alrm->command);
}

void Plugin::create_options() {
    xfce_panel_plugin_block_menu(plugin);

    Gtk::Dialog *dlg = Glib::wrap(GTK_DIALOG(xfce_titled_dialog_new_with_buttons (
                    _("Xfce4 Timer Options"),
                    GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (plugin))),
                    GTK_DIALOG_DESTROY_WITH_PARENT, _("Close"), GTK_RESPONSE_OK, NULL
                    )));
    dlg->set_icon_name("xfce4-timer-plugin");

    auto *dialog_vbox = dlg->get_content_area();

    auto vbox = Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6);
    dialog_vbox->pack_start(vbox, true, true, 0);

    dlg->signal_response().connect([=](int response_id){ options_dialog_response(dlg, response_id); });

    vbox.set_border_width(10);

    dlg->set_size_request(650, -1);
    dlg->set_position(Gtk::WIN_POS_CENTER);

    auto hbox = Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12);
    vbox.pack_start (hbox, true, true, 0);

    auto sw = Gtk::ScrolledWindow();

    sw.set_shadow_type (Gtk::SHADOW_ETCHED_IN);
    sw.set_policy (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

    hbox.pack_start (sw, true, true, 0);

    fill_liststore();

    tree = std::make_unique<Gtk::TreeView>(liststore);
    tree->get_selection()->set_mode(Gtk::SELECTION_SINGLE);

    tree->append_column(_("Timer name"), columns.name);
    tree->append_column(_("Countdown period /\nAlarm time"), columns.info);
    tree->append_column(_("Alarm command"), columns.command);

    sw.add(*tree);

    sw.set_size_request(350, 200);

    auto select = tree->get_selection();
    select->signal_changed().connect([&](){ tree_selected(true); });
    select->set_mode(Gtk::SELECTION_SINGLE);
    select->signal_changed().connect([&](){ tree_selected(true); });

    auto buttonbox = Gtk::ButtonBox(Gtk::ORIENTATION_VERTICAL);
    buttonbox.set_layout(Gtk::BUTTONBOX_START);
    buttonbox.set_spacing(6);
    hbox.pack_start(buttonbox, false, false, 0);

    buttonadd = std::make_unique<Gtk::Button>(_("Add"));
    buttonbox.pack_start(*buttonadd, false, false, 0);
    buttonadd->set_sensitive(true);
    buttonadd->signal_clicked().connect([&](){ add_alarm(std::move(AddEditDialog(*dlg, nullptr).newalarm)); });

    buttonedit = std::make_unique<Gtk::Button>(_("Edit"));
    buttonbox.pack_start(*buttonedit, false, false, 0);
    buttonedit->set_sensitive(false);
    buttonedit->signal_clicked().connect([&](){ AddEditDialog(*dlg, selected_row()->get_value(columns.alrm)); update_selected(); });

    buttonremove = std::make_unique<Gtk::Button>(_("Remove"));
    buttonbox.pack_start(*buttonremove, false, false, WIDGET_SPACING);
    buttonremove->set_sensitive(false);
    buttonremove->signal_clicked().connect(sigc::mem_fun(this, &Plugin::remove_clicked));

    buttonup = std::make_unique<Gtk::Button>(_("Up"));
    buttonbox.pack_start(*buttonup, false, false, WIDGET_SPACING);
    buttonup->set_sensitive(false);
    buttonup->signal_clicked().connect(sigc::mem_fun(this, &Plugin::up_clicked));

    buttondown = std::make_unique<Gtk::Button>(_("Down"));
    buttonbox.pack_start(*buttondown, false, false, WIDGET_SPACING);
    buttondown->set_sensitive(false);
    buttondown->signal_clicked().connect(sigc::mem_fun(this, &Plugin::down_clicked));

    hbox.set_size_request(-1, -1);

    auto sep0 = Gtk::Separator();
    vbox.pack_start(sep0, false, false, BORDER);

    auto cbtn_warn = Gtk::CheckButton(_("Don't display a warning  if an alarm command is set"));
    cbtn_warn.set_active(nowin_if_alarm);
    cbtn_warn.signal_toggled().connect([&](){ nowin_if_alarm = cbtn_warn.get_active(); });
    vbox.pack_start(cbtn_warn, false, false, WIDGET_SPACING);

    auto sep1 = Gtk::Separator();
    vbox.pack_start(sep1, false, false, BORDER);

    /* Default alarm command config */
    auto cbtn_default_cmd = Gtk::CheckButton(_("Use a default alarm command"));
    cbtn_default_cmd.set_active(use_global_command);
    cbtn_default_cmd.signal_toggled().connect([&](){ global_command_box->set_sensitive(use_global_command = cbtn_default_cmd.get_active()); });
    vbox.pack_start(cbtn_default_cmd, false, false, WIDGET_SPACING);

    global_command_box = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL);
    auto label_default_cmd = Gtk::Label(_("Default command: "));
    global_command_box->pack_start(label_default_cmd, false, false, 0);
    global_command_box->set_margin_start (12);
    glob_command_entry = std::make_unique<Gtk::Entry>();
    glob_command_entry->set_size_request(400, -1);
    glob_command_entry->set_text(global_command);
    global_command_box->pack_start(*glob_command_entry, false, false, 10);

    vbox.pack_start(*global_command_box, false, false, WIDGET_SPACING);
    global_command_box->set_sensitive(use_global_command);

    auto sep2 = Gtk::Separator();
    vbox.pack_start(sep2, false, false, BORDER); 

    /* Alarm repetitions config */
    auto cbtn_repeat = Gtk::CheckButton(_("Repeat the alarm command"));
    cbtn_repeat.set_active(repeat_alarm_command);
    cbtn_repeat.signal_toggled().connect([&](){ repeat_alarm_box->set_sensitive(repeat_alarm_command = cbtn_repeat.get_active()); });
    vbox.pack_start(cbtn_repeat, false, false, WIDGET_SPACING);

    repeat_alarm_box = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    repeat_alarm_box->set_margin_start(12);

    auto label_num_rep = Gtk::Label(_("Number of repetitions"));
    repeat_alarm_box->pack_start (label_num_rep, false, false, 0);
    spin_repeat = std::make_unique<SpinButtonInt>(1, 50);
    spin_repeat->set_value(repetitions);
    spin_repeat->signal_value_changed().connect([&](){ repetitions = spin_repeat->get_value_as_int(); });
    repeat_alarm_box->pack_start(*spin_repeat, false, false, 10);

    auto label_time_interval = Gtk::Label(_("  Time interval (sec.)"));
    repeat_alarm_box->pack_start(label_time_interval, false, false, 0);
    spin_interval = std::make_unique<SpinButtonInt>(1, 600);
    repeat_alarm_box->pack_start(*spin_interval, false, false, 10);
    spin_interval->set_value(repeat_interval);
    spin_interval->signal_value_changed().connect([&](){ repeat_interval = spin_interval->get_value_as_int(); });

    vbox.pack_start(*repeat_alarm_box, false, false, WIDGET_SPACING);
    repeat_alarm_box->set_sensitive(repeat_alarm_command);

    dlg->set_modal(true);
    dlg->show_all();
    dlg->run();
}

void Plugin::options_dialog_response(Gtk::Dialog *dlg, int reponse_id)
{
    global_command = glob_command_entry->get_text();
    dlg->close();
    xfce_panel_plugin_unblock_menu (plugin);
    save_settings();
}

template <typename T, typename D>
std::unique_ptr<T, D> wrap_raw(T* p, D d) {
    return {p, d};
}

void Plugin::save_settings()
{
    auto file = wrap_raw(xfce_panel_plugin_save_location(plugin, TRUE), g_free);
    if (!file)
        return;

    /**
     * We do this to start a fresh config file, otherwise if the old config file
     * is longer,   the tail will not get truncated
     * See http://bugzilla.xfce.org/show_bug.cgi?id=2647
     * for a related bug report
     **/
    { std::ofstream(file); }

    auto rc = Xfce::Rc(file.get(), false);
    if (!rc)
        return;

    int row_count = 0;
    for (const auto &alrm: alarm_list) {
        rc.set_group("G" + std::to_string(row_count++));
        alrm->to_xfce_rc(rc);
    }

    /* save the other options */
    rc.set_group("others");
    rc.write("nowin_if_alarm",     nowin_if_alarm);
    rc.write("use_global_command", use_global_command);
    rc.write("global_command",     global_command);
    rc.write("repeat_alarm",       repeat_alarm_command);
    rc.write("repetitions",        repetitions);
    rc.write("repeat_interval",    repeat_interval);
}

void Plugin::load_settings() {
    auto rc_path = wrap_raw(xfce_panel_plugin_lookup_rc_file(plugin), g_free);
    if (!rc_path)
        return;

    auto rc = Xfce::Rc(rc_path.get(), TRUE);
    if (!rc)
        return;

    std::string groupname;
    for (int groupnum=0; rc.has_group(groupname = "G"+std::to_string(groupnum)); ++groupnum) {
        rc.set_group(groupname);
        alarm_list.push_back(Alarm::from_xfce_rc(rc));
    }

    /* Read other options */
    if (rc.has_group("others")) {
        rc.set_group("others");
        nowin_if_alarm = rc.read("nowin_if_alarm", false);
        use_global_command = rc.read("use_global_command", false);
        global_command = rc.read("global_command", "");
        repeat_alarm_command = rc.read("repeat_alarm", false);
        repetitions = rc.read("repetitions", 1);
        repeat_interval = rc.read("repeat_interval", 10);
    }

    update_pbar_orientation();
}

void Plugin::remove_clicked() {
    auto select = selected_row();

    if (!select)
        return;

    Alarm *alrm = selected_row()->get_value(columns.alrm);
    int idx = get_alarm_index(alrm);
    alarm_list.erase(alarm_list.begin() + idx);
    liststore->erase(select);
    if (!selected_row()) {
        tree_selected(false);
    }
}

void Plugin::up_clicked() {
    auto select = selected_row();

    if (!select)
        return;

    Alarm *alrm = selected_row()->get_value(columns.alrm);
    int idx = get_alarm_index(alrm);
    if (idx == 0)
        return;

    std::swap(alarm_list[idx-1], alarm_list[idx]);
    liststore->iter_swap(select, std::prev(select));
}

void Plugin::down_clicked() {
    auto select = selected_row();

    if (!select)
        return;

    Alarm *alrm = selected_row()->get_value(columns.alrm);
    int idx = get_alarm_index(alrm);
    if (idx == alarm_list.size()-1)
        return;

    std::swap(alarm_list[idx], alarm_list[idx+1]);
    liststore->iter_swap(select, std::next(select));
}

bool Plugin::pbar_clicked(GdkEventButton *btn) {
    if (btn->button != 1)
        return false;

    menu = std::make_unique<Gtk::Menu>();

    bool need_separator = false;
    for (const auto &alrm: alarm_list) {
        Alarm *const alrmp = alrm.get();

        if (need_separator)
            menu->append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());
        need_separator = true;

        auto name_item = Gtk::make_managed<Gtk::MenuItem>(Glib::ustring::sprintf("%s (%s)", alrmp->name, alrmp->info));
        menu->append(*name_item);
        name_item->set_sensitive(true);
        name_item->set_sensitive(!alrmp->timer_on && !alrmp->is_repeating);
        name_item->signal_activate().connect([=](){ start_stop_callback(alrmp); });

        /* The selected timer is always active */
        if (alrmp->timer_on) {
            /* If the alarm is paused, the only option is to resume or stop */
            if (alrmp->is_paused || alrmp->is_countdown) {
                auto item = Gtk::make_managed<Gtk::MenuItem>(alrmp->is_paused ?_("Resume timer") :_("Pause timer"));
                menu->append(*item);
                item->signal_activate().connect([=](){ pause_resume_selected(alrmp); });
            }

            auto item = Gtk::make_managed<Gtk::MenuItem>(_("Stop timer"));
            menu->append(*item);
            item->signal_activate().connect([=](){ start_stop_callback(alrmp); });
            auto itm=gtk_menu_item_new_with_label(_("Stop timer"));
        }
    }

    if (alarm_list.empty()) {
        auto item = Gtk::make_managed<Gtk::MenuItem>("(No alarm)");
        item->set_sensitive(false);
        menu->append(*item);
    }

    // menu->signal_hide().connect([=](){ menu.reset(); });  CANNOT DO THIS
    menu->show_all();
	menu->popup_at_widget(pbar.get(), Gdk::GRAVITY_SOUTH_WEST, Gdk::GRAVITY_NORTH_WEST, nullptr);
    return true;
}

/**
 * This is the callback function called when the
 * start/stop item is selected in the popup menu
 **/
void Plugin::start_stop_callback(Alarm *alrm) {
    if (!alrm->timer_on) {
        /* If we're here then the timer is off, so we start it */
        start_timer(alrm);
        return;
    }

    /* If counting down, we stop the timer and free the resources */
    if(alrm->timer)
        alrm->timer.reset();
    if(alrm->timeout)
        alrm->timeout.disconnect();

    alrm->is_paused = false;
    alrm->timer_on = false;

    /* Disable tooltips, reset pbar */
    gtk_widget_set_tooltip_text(GTK_WIDGET(plugin), "");
    pbar->set_fraction(0);
}

/**
 * Used for starting/rerunning the timer
 * Assumes that the timer is already stopped
 **/
void Plugin::start_timer(Alarm* alrm)
{
    /* Empty timer list-> Nothing to do. alrm=0, though */
    if (!alrm)
        return;

    pbar->set_fraction(1);

    int timeout_period;

    /**
     *  If it's a 24h type alarm, we find the difference with current time
     *  Here 'time' is in minutes
     **/
    if (!alrm->is_countdown) {

        auto current = Glib::DateTime::create_now_local();
        timeout_period = (alrm->time) * 60 - ((60 * current.get_hour() + current.get_minute()) * 60 + current.get_second());

        if (timeout_period < 0)
            timeout_period += 24 * 60 * 60;
    }
    /* Else 'alrm->selected->time' already gives the countdown period in seconds */
    else {
        timeout_period = alrm->time;
    }

    alrm->timeout_period_in_sec = timeout_period;

    /* start the timer */
    alrm->timer = std::make_unique<Glib::Timer>();
    alrm->timer_on = true;

    gtk_widget_set_tooltip_text(GTK_WIDGET(plugin), alrm->info.c_str());

    alrm->timer->start();
    alrm->timeout = Glib::signal_timeout().connect(sigc::mem_fun(this, &Plugin::update_function), UPDATE_INTERVAL);
}

void Plugin::pause_resume_selected(Alarm* alrm) {
    if (alrm->is_paused)
        g_timer_continue(alrm->timer->gobj());
    else
        alrm->timer->stop();

    alrm->is_paused ^= 1;
}

bool Plugin::update_function() {
    //for showing the progress of the first to finish
    int min_remaining_time = INT_MAX;
    Glib::ustring finalTipText;
    bool callAgain = false;
    bool firstActiveTimer = true;

    for (const auto &alrm: alarm_list) {
        Alarm *alarmp = alrm.get();
        if (!alarmp->timer_on)
            continue;

        Glib::ustring tiptext;
        int elapsed_sec = alarmp->timer->elapsed(); // (gint) g_timer_elapsed(alarmp->timer, NULL);
        /* If countdown is not over, update tooltip */
        if (elapsed_sec < alarmp->timeout_period_in_sec) {
            int remaining = alarmp->timeout_period_in_sec - elapsed_sec;

            if (remaining >= 3600)
                tiptext = Glib::ustring::sprintf(_("%dh %dm %ds left"), remaining / 3600, (remaining % 3600) / 60, remaining % 60);
            else if (remaining >= 60)
                tiptext = Glib::ustring::sprintf(_("%dm %ds left"), remaining / 60, remaining % 60);
            else
                tiptext = Glib::ustring::sprintf(_("%ds left"), remaining);

            if (alarmp->is_paused)
                tiptext += " (Paused)";
            if(alarmp->timeout_period_in_sec < min_remaining_time){
                min_remaining_time = alarmp->timeout_period_in_sec;
                pbar->set_fraction(1.0 - static_cast<double>(elapsed_sec) / alarmp->timeout_period_in_sec);
            }
            callAgain = true;
        } else {

            /* Countdown is over, stop timer and free resources */
            alarmp->timer.reset();

            /* Disable tooltips, reset pbar */
            gtk_widget_set_tooltip_text(GTK_WIDGET(plugin), "");
            pbar->set_fraction(0);

            alarmp->timeout.disconnect();
            alarmp->timer_on = false;

            std::string command;
            /* If an alarm command is set, it overrides the default (if any) */
            if (!alarmp->command.empty())
                command = alarmp->command;
            else if (use_global_command)
                command = global_command;

            if (command.empty() || !nowin_if_alarm) {
                pbar->set_fraction(0);

                /* Display the name of the alarm when the countdown ends */
                auto dialog_message = Glib::ustring::sprintf(_("Beeep! :) \nTime is up for the alarm %s."), alarmp->name);
                auto dialog_title = Glib::ustring::sprintf("Xfce4 Timer Plugin: %s", alarmp->name);

                auto dialog = new Gtk::MessageDialog(dialog_message, false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_NONE, true);
                dialog->set_title(dialog_title);
                dialog->add_button(_("Close"), 0);
                dialog->add_button(_("Rerun the timer"), 1);
                dialog->signal_response().connect([=](int id){ if (id==1) start_timer(alarmp); delete dialog; });
                dialog->show_all();
            }

            if (!command.empty()) {
                try {
                    Glib::spawn_command_line_async(command);
                } catch (const Glib::Error&) {
                }

                if (repeat_alarm_command) {
                    alarmp->is_repeating = true;
                    alarmp->rem_repetitions = repetitions;
                    if (alarmp->repeat_timeout)
                        alarmp->repeat_timeout.disconnect();
                    alarmp->timeout = Glib::signal_timeout().connect([=](){ return repeat_alarm(alarmp); }, repeat_interval * 1000);
                }
            }

            //Check if alarm is recurring after it's finished and destroyed; if yes then start it again.
            if (alarmp->is_recurring) {
                start_timer(alarmp);
            }

        }
        tiptext = alarmp->name + "\t" + tiptext;
        if (!firstActiveTimer) {
            tiptext = "\n" + tiptext;
        }
        firstActiveTimer = false;
        finalTipText += tiptext;
    }
    gtk_widget_set_tooltip_text(GTK_WIDGET(plugin), finalTipText.c_str());
    return callAgain;
}

bool Plugin::repeat_alarm(Alarm *alrm) {
    /* Don't repeat anymore */
    if (alrm->rem_repetitions == 0) {
        alrm->is_repeating=false;
        return false;
    }

    std::string command;
    if (!alrm->command.empty())
        command = alrm->command;
    else if (use_global_command)
        command = global_command;

    try {
        Glib::spawn_command_line_async(command);
    } catch (const Glib::Error&) {
    }
    --alrm->rem_repetitions;
    return true;;
}
