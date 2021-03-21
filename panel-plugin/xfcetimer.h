#include <memory>
#include <iostream>
#include <gtkmm.h>
#include <assert.h>
extern "C" {
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4panel/libxfce4panel.h>
}

namespace Xfce {
    class Rc {
        private:
            XfceRc *raw;

        public:
            Rc(const std::string& path, bool readonly): raw(xfce_rc_simple_open(path.c_str(), readonly)) { }
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
    
    struct PanelPluginPtr {
        XfcePanelPlugin *raw;
        PanelPluginPtr(XfcePanelPlugin *raw_): raw(raw_) { }
        
        std::string get_save_location() {
            char* s = xfce_panel_plugin_save_location(raw, TRUE);
            if (!s)
                return {};
            std::string result(s);
            g_free(s);
            return std::move(result);
        }
        
        template <typename...Args>
        Gtk::Dialog *titled_dialog(const Glib::ustring &title, Args...args) {
            return Glib::wrap(GTK_DIALOG(xfce_titled_dialog_new_with_buttons(
                title.c_str(),
                GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(raw))),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                args..., NULL)));
        }

        void block_menu() { xfce_panel_plugin_block_menu(raw); }
        void unblock_menu() { xfce_panel_plugin_unblock_menu(raw); }
        Gtk::Orientation get_orientation() { return static_cast<Gtk::Orientation>(xfce_panel_plugin_get_orientation(raw)); }
        void menu_show_configure() { xfce_panel_plugin_menu_show_configure(raw); }
        void menu_show_about() { xfce_panel_plugin_menu_show_about (raw); };
        int get_size() { return xfce_panel_plugin_get_size(raw); }
        void set_size_request(int width, int height) { gtk_widget_set_size_request(GTK_WIDGET(raw), width, height); }
        void set_tooltip_text(const Glib::ustring& text) { gtk_widget_set_tooltip_text(GTK_WIDGET(raw), text.c_str()); }
        void add(Gtk::Widget& w) { gtk_container_add(GTK_CONTAINER(raw), w.gobj()); }
        void show_all() { gtk_widget_show_all (GTK_WIDGET (raw)); }
        
        template <typename L>
        void connect_signal(const char *signal_detail, L l) {
            connect_signal_helper(signal_detail, l, &L::operator());
        }

    private:
        template <typename L, typename R, typename... Args>
        void connect_signal_helper(const char *signal_detail, L& l, R (L::*)(Args...) const) {
            using F = std::function<R(Args...)>;
            g_signal_connect_data(
                raw, signal_detail,
                reinterpret_cast<GCallback>(+[](decltype(raw) r, Args... args, F *p) { return (*p)(args...); }),
                new F(std::move(l)),
                reinterpret_cast<GClosureNotify>(+[](F *p, GClosure*) { delete p; }),
                static_cast<GConnectFlags>(0));
        }
    };

    Glib::RefPtr<Gdk::Pixbuf> panel_pixbuf(std::string source, Gtk::IconTheme* icon_theme, int size) {
        return Glib::wrap(xfce_panel_pixbuf_from_source (source.c_str(), icon_theme ?icon_theme->gobj() :nullptr, size));
    }

    using LicenceType = XfceLicenseTextType;
    Glib::ustring get_license_text(LicenceType type) { return xfce_get_license_text (type); }
    
    void textdomain(const std::string& package, const std::string& localedir, const std::string& encoding) {
        xfce_textdomain(package.c_str(), localedir.c_str(), encoding.c_str());
    }
}

class Alarm {
    static constexpr int DAY_SECS = 60 * 60 * 24;

public:
    enum State {
        OFF,
        PAUSED,
        ON,
    };

    Glib::ustring name, info;
    std::string command;
    int time;
    bool is_recurring, is_auto_start;
    State state = OFF; // replacing:  bool timer_on, is_paused;

    bool is_countdown; /* True if the alarm type is contdown */
    Glib::Timer timer; /* Keeps track of the time elapsed */

    static std::unique_ptr<Alarm> from_xfce_rc(const Xfce::Rc &rc)
    {
        auto alrm = std::make_unique<Alarm>();
        alrm->name = rc.read("timername", "No name");
        alrm->command = rc.read("timercommand", "");
        alrm->info = rc.read("timerinfo", "");
        alrm->is_countdown = rc.read("is_countdown", true);
        alrm->is_recurring = rc.read("is_recur", false);
        alrm->is_auto_start = rc.read("autostart", false);
        alrm->time = rc.read("time", 0);
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

		int rem_repetitions = 0;
		void start() { state = ON; timer.start(); }
		void pause() { state = PAUSED; timer.stop(); }
		void resume() { state = ON; g_timer_continue(timer.gobj()); }
		void stop()  { state = OFF; timer.reset(); }
		void toggle() { state==ON ?pause() :resume(); }
		int remain() const;
		double remain_progress() { return static_cast<double>(remain()) / (is_countdown ?time :DAY_SECS); }

		Glib::ustring get_remaining_info() {
			assert(state != OFF);
            int secs = remain();
			int h = secs / 3600;
			int m = secs / 60 % 60;
			int s = secs % 60;
			return format_time(_("%dh %dm %ds left"), _("%dm %ds left"), _("%ds left"), h, m, s);
		}

		static Glib::ustring format_time(const char *fhms, const char *fms, const char *fs, int h, int m, int s) {
			if (h > 0)
				return Glib::ustring::sprintf(fhms, h, m, s);
			else if (m > 0)
				return Glib::ustring::sprintf(fms, m, s);
			else
				return Glib::ustring::sprintf(fs, s);
		}

		Glib::ustring get_tag() {
			Glib::ustring result = name;
			if (state != OFF)
				result += ": " + get_remaining_info();
			if (state == PAUSED)
				result += " (Paused)";
			return result;
		}
};

class AlarmMenu: public Gtk::Menu {
	sigc::connection update_receiver;
	sigc::signal<void()> signal_update_items;
	const std::vector<std::unique_ptr<Alarm>>& alarms;

	public:
	sigc::signal<void()> signal_alarm_change, signal_settings;

    AlarmMenu(sigc::signal<void()>& signal_update, decltype(alarms)&);
	~AlarmMenu() { update_receiver.disconnect(); }
};

class Plugin {
    private:
        Xfce::PanelPluginPtr panel_plugin;

        int size = 0;
        std::unique_ptr<Gtk::Box> box;
        Gtk::ProgressBar* pbar = nullptr;
        Gtk::Button* buttonadd = nullptr;
        Gtk::Button* buttonedit = nullptr;
        Gtk::Button* buttonremove = nullptr;
        Gtk::Button* buttonup = nullptr;
        Gtk::Button* buttondown = nullptr;
        Gtk::Box* global_command_box = nullptr;
        Gtk::Entry* glob_command_entry = nullptr;
        Gtk::Box* repeat_alarm_box = nullptr;
        Gtk::SpinButton* spin_repeat = nullptr;
        Gtk::SpinButton* spin_interval = nullptr;
        Gtk::TreeView* tree = nullptr;
        std::unique_ptr<AlarmMenu> menu;
        Glib::RefPtr<Gtk::ListStore> liststore;

        bool nowin_if_alarm = false;
        bool use_global_command = false;
        std::string global_command;
        bool repeat_alarm_command = false;
        int repetitions = 1;
        int repeat_interval = 10;
        
        sigc::connection heartbeat_timeout;
        sigc::signal<void()> signal_update;

        std::vector<std::unique_ptr<Alarm>> alarm_list;

        void set_row(Gtk::TreeModel::iterator iter, Alarm *alrm);
        void add_alarm(std::unique_ptr<Alarm> newalarm);
        Gtk::TreeModel::iterator selected_row() { return tree->get_selection()->get_selected(); }
        void update_selected() { set_row(selected_row(), selected_row()->get_value(columns.alrm)); }
        int get_alarm_index(Alarm *alrm) {
            return std::distance(alarm_list.begin(),
                                 std::find_if(alarm_list.begin(), alarm_list.end(),
                                              [=](const auto &p) { return p.get() == alrm; }));
        }

		void beep_alarm(Alarm*);
        void start_timer(Alarm*);
        void enable_heartbeat();

    public:
        Plugin(const Plugin&) = delete;
        Plugin(Xfce::PanelPluginPtr);

        void show_about();
        bool size_changed(int size);
        void update_pbar_orientation();
        void create_options();
        void load_settings();
        void save_settings();

        void options_dialog_response(Gtk::Dialog*, int respone_id);
        void add_clicked(Gtk::Window& toplevel);
        void edit_clicked(Gtk::Window& toplevel);
        void remove_clicked();
        void up_down_remove_clicked(int delta);
        void tree_selected(bool selected) {
            for (auto *btn: {buttonedit, buttonremove, buttonup, buttondown})
                btn->set_sensitive(selected);
        }

        bool pbar_clicked(GdkEventButton*);
        bool update_function();

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
