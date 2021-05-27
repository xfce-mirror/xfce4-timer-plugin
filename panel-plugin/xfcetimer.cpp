#include <gtkmm.h>
#include <assert.h>
#include <fstream>

#include "xfcetimer.h"

extern "C" {
	XFCE_PANEL_PLUGIN_REGISTER(+[](XfcePanelPlugin *raw) { new Plugin(Xfce::PanelPluginPtr(raw)); });
}

constexpr int UPDATE_INTERVAL = 2000;
constexpr int PBAR_THICKNESS = 10;
constexpr int BORDER = 4;
constexpr int WIDGET_SPACING = 2;

using Gtk::make_managed;
using Glib::ustring;

// delete self upon delete event
void register_self_managed(Gtk::Window* w) {
	w->signal_delete_event().connect([=](GdkEventAny*){ delete w; return true; });
}

template <typename T, typename...Args>
T* make_self_managed(Args&&...args) {
	T* t = new T(std::forward<Args>(args)...);
	register_self_managed(t);
	return t;
}

void run_command(const std::string& cmd) {
	try {
		Glib::spawn_command_line_async(cmd);
	} catch (const Glib::Error&) {
	}
}

class BeepDialog: public Gtk::MessageDialog {
	public:
		sigc::signal<void()> signal_repeat;
		BeepDialog(const std::string& name): Gtk::MessageDialog(
				ustring::sprintf(_("Beeep! :) \nTime is up for the alarm %s."), name), // message
				false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_NONE, true) {
			set_title(_("Xfce4 Timer Plugin: ") + name);
			add_button(_("Close"), 0);
			add_button(_("Rerun the timer"), 1);
			signal_response().connect([=](int id){ if (id==1) signal_repeat.emit(); close(); });
		}
};

Plugin::Plugin(Xfce::PanelPluginPtr panel_plugin_): panel_plugin(panel_plugin_) {
	Gtk::Main::init_gtkmm_internals();  // for Glib::wrap

	Xfce::textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");


	pbar = make_managed<Gtk::ProgressBar>();
	liststore = Gtk::ListStore::create(columns);
	box = std::make_unique<Gtk::Box>();

	panel_plugin.set_tooltip_text("");

	load_settings();

	//Check if an alarm is auto start to start it at creation
	for (const auto &alrm: alarm_list) {
		if (alrm->is_auto_start){
			start_timer(alrm.get());
		}
	}

	box->set_border_width(BORDER / 2);
	panel_plugin.add(*box);

	pbar->set_fraction(0.0);
	box->pack_start(*pbar, false, false, 0);

	update_pbar_orientation();

	panel_plugin.connect_signal("button_press_event", [=](GdkEventButton *btn){ return pbar_clicked(btn); });
	panel_plugin.show_all();

	panel_plugin.connect_signal("free-data", [=]{ delete this; Gtk::Main::quit(); });
	panel_plugin.connect_signal("save", [=]{ save_settings(); });
	panel_plugin.connect_signal("orientation-changed", [this](GtkOrientation){ update_pbar_orientation(); });
	panel_plugin.connect_signal("size-changed", [=](int size){ return size_changed(size); });

	panel_plugin.menu_show_configure();
	panel_plugin.connect_signal("configure-plugin", [=]{ create_options(); });

	panel_plugin.menu_show_about();
	panel_plugin.connect_signal("about", [=]{ show_about(); });
}


void Plugin::beep_alarm(Alarm* alrm) {
	std::string* pcmd = nullptr;
	if (!alrm->command.empty())
		pcmd = &alrm->command;
	else if (use_global_command)
		pcmd = &global_command;

	if (!pcmd || !nowin_if_alarm)  {
		auto *dialog = make_self_managed<BeepDialog>(alrm->name);
		dialog->signal_repeat.connect([=]{ start_timer(alrm); });
		dialog->show_all();
	}

	if (pcmd) {
		int n_rep = repeat_alarm_command ?repetitions :1;
		auto f = [cmd = *pcmd, cnt = n_rep]() mutable { run_command(cmd); return --cnt>0; };
		if (f())
			 Glib::signal_timeout().connect(std::move(f), repeat_interval * 1000);
	}
}

	int remain_if_on0(const Alarm& alrm){ return alrm.state!=Alarm::OFF ?alrm.remain() :INT_MAX; };
bool Plugin::update_function() {
	// beeps if any alarm times up
	for (const auto &alrm: alarm_list) {
		if (alrm->state!=Alarm::ON || alrm->remain() > 0)
			continue;

		beep_alarm(alrm.get());
		alrm->stop();
		if (alrm->is_recurring)
			alrm->start();
	}

	signal_update.emit();

	// find minimum remaining progress among all active alarms
	// int (*remain_if_on)(const Alarm&) = +[](const Alarm& alrm){ return alrm.state!=Alarm::OFF ?alrm.remain() :INT_MAX; };
	constexpr auto *remain_if_on = +[](const Alarm& alrm){ return alrm.state!=Alarm::OFF ?alrm.remain() :INT_MAX; };
	auto &p = *std::min_element(alarm_list.begin(), alarm_list.end(),
								[](auto &a1, auto &a2) { return remain_if_on(*a1) < remain_if_on(*a2); });
	// make sure the progress is visually non empty if any alarm is active
	pbar->set_fraction(p->state!=Alarm::OFF ?std::max(2.0/size, p->remain_progress()) :0.0);
	
	// continue heartbeat iff any alarm is still on
	return std::any_of(alarm_list.begin(), alarm_list.end(), [](auto &alrm) { return alrm->state == Alarm::ON; });
}

bool Plugin::size_changed(int size)
{
	this->size = size;
	if (panel_plugin.get_orientation() == Gtk::ORIENTATION_HORIZONTAL)
		panel_plugin.set_size_request(-1, size);
	else
		panel_plugin.set_size_request(size, -1);

	return true;
}

void Plugin::start_timer(Alarm* alrm) {
	alrm->start();
	enable_heartbeat();
}

void Plugin::enable_heartbeat() {
	heartbeat_timeout.disconnect();
	if (update_function()) {
		heartbeat_timeout = Glib::signal_timeout().connect([=] { return update_function(); }, UPDATE_INTERVAL);
	}
}

void Plugin::update_pbar_orientation()
{
	if (panel_plugin.get_orientation() == Gtk::ORIENTATION_HORIZONTAL)
	{
		/* vertical bar */
		box->set_orientation(Gtk::ORIENTATION_HORIZONTAL);
		pbar->set_orientation(Gtk::ORIENTATION_VERTICAL);

		pbar->set_inverted(true);
		pbar->set_halign(Gtk::ALIGN_CENTER);
		pbar->set_hexpand(true);

		panel_plugin.set_size_request(-1, panel_plugin.get_size());
	}
	else
	{
		/* horizontal bar */
		box->set_orientation(Gtk::ORIENTATION_VERTICAL);
		pbar->set_orientation(Gtk::ORIENTATION_HORIZONTAL);

		pbar->set_inverted(false);
		pbar->set_halign(Gtk::ALIGN_CENTER);
		pbar->set_hexpand(false);

		panel_plugin.set_size_request(panel_plugin.get_size(), -1);
	}
}


void Plugin::show_about() {
	const std::vector<ustring> author{ "Kemal Ilgar Eroğlu <ilgar_eroglu@yahoo.com>"};
	const ustring translators (
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

	auto icon = Xfce::panel_pixbuf("xfce4-timer-plugin", nullptr, 48);
	auto dialog = make_self_managed<Gtk::AboutDialog>();
	dialog->set_title(_("About xfce4-timer-plugin"));
	dialog->set_logo(icon);
	dialog->set_license(Xfce::get_license_text(XFCE_LICENSE_TEXT_GPL));
	dialog->set_version(PACKAGE_VERSION);
	dialog->set_program_name(PACKAGE_NAME);
	dialog->set_comments(_("A plugin to define countdown timers or alarms at given times."));
	dialog->set_website("https://docs.xfce.org/panel-plugins/xfce4-timer-plugin");
	dialog->set_copyright(_("Copyright (c) 2005-2018"));
	dialog->set_authors(author);
	dialog->set_translator_credits(translators);
	dialog->show_all();
}

class SpinButtonInt: public Gtk::SpinButton {
	public:
		SpinButtonInt(int min, int max) {
			set_range(min, max);
			set_increments(1, 10);
		}
};

class AddEditDialog: public Gtk::Dialog {
	Gtk::SpinButton *timeh, *times, *timem; /* Spinbuttons for h-m-s format */
	Gtk::SpinButton *time_h, *time_m; /* Spinbuttons for 24h format */
	Gtk::Entry *name, *command; /* Name, and command entries */
	Gtk::RadioButton *rb1; /* Radio button for the h-m-s format */
	Gtk::CheckButton *recur_cb, *autostart_cb; /* check buttons for recurring alarm, autostart */

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
			alrm->info = Alarm::format_time(_("%dh %dm %ds"), _("%dm %ds"), _("%ds"), t1, t2, t3);
		} else {
			/* The 24h format (alarm at specified time). Save time in minutes */
			int t1 = time_h->get_value_as_int();
			int t2 = time_m->get_value_as_int();
			alrm->time = t1 * 3600 + t2 * 60;
			alrm->info = ustring::sprintf(_("At %02d:%02d"), t1, t2);
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
		// add(*make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6));

		auto *box = get_content_area();
		box->set_border_width(2);
		box->set_spacing(6);

		auto *vbox = make_managed<Gtk::VBox>(false, 6);
		box->pack_start(*vbox, false, false, 0);
		vbox->set_border_width(12);

		/***********/
		auto *hbox = make_managed<Gtk::HBox>(false, 12);
		vbox->pack_start(*hbox, false, false, 0);

		name = make_managed<Gtk::Entry>();

		hbox->pack_start(*make_managed<Gtk::Label>(_("Name:")), false, false, 0);
		hbox->pack_start(*name, true, true, 0);

		vbox->pack_start(*make_managed<Gtk::Separator>(), false, false, 6);

		/**********/
		rb1 = make_managed<Gtk::RadioButton>(_("Enter the countdown time"));
		rb1->signal_toggled().connect([=]{ alarmdialog_countdown_toggled(*rb1); });
		auto rb_group = rb1->get_group();
		auto *rb2 = make_managed<Gtk::RadioButton>(rb_group, _("Enter the time of alarm (24h format)"));
		rb2->signal_toggled().connect([=]{ alarmdialog_alarmtime_toggled(*rb2); });

		vbox->pack_start(*rb1, true, true, 0);

		hbox = make_managed<Gtk::HBox>(false, 6);
		vbox->pack_start(*hbox, true, true, 0);
		hbox->set_margin_start(12);

		timeh = make_managed<SpinButtonInt>(0, 23);
		hbox->pack_start(*timeh, false, false, 0);
		hbox->pack_start(*make_managed<Gtk::Label>(_("h ")), false, false, 0);

		timem = make_managed<SpinButtonInt>(0, 59);
		hbox->pack_start(*timem, false, false, 0);
		hbox->pack_start(*make_managed<Gtk::Label>(_("m ")), false, false, 0);

		times = make_managed<SpinButtonInt>(0, 59);
		hbox->pack_start(*times, false, false, 0);
		hbox->pack_start(*make_managed<Gtk::Label>(_("s ")), false, false, 0);

		hbox->pack_start(*make_managed<Gtk::Label>(_("or ")), false, false, 0);
		vbox->pack_start(*rb2, true, true, 0);

		hbox = make_managed<Gtk::HBox>(false, 6);
		vbox->pack_start(*hbox, true, true, 0);
		hbox->set_margin_start(12);

		time_h = make_managed<SpinButtonInt>(0, 23);
		hbox->pack_start(*time_h, false, false, 0);
		hbox->pack_start(*make_managed<Gtk::Label>(_(":")), false, false, 0);
		time_m = make_managed<SpinButtonInt>(0, 59);
		hbox->pack_start(*time_m, false, false, 0);

		vbox->pack_start(*make_managed<Gtk::Separator>(), false, false, 6);

		/****************/

		hbox = make_managed<Gtk::HBox>(false, 12);
		vbox->pack_start(*hbox, true, true, 0);
		hbox->pack_start(*make_managed<Gtk::Label>(_("Command to run:")), false, false, 0);

		command = make_managed<Gtk::Entry>();
		hbox->pack_start(*command, true, true, 0);

		/****************/

		vbox->pack_start(*make_managed<Gtk::Separator>(), false, false, 6);

		//add recurring alarm check button
		recur_cb = make_managed<Gtk::CheckButton>(_("Recurring alarm"));
		recur_cb->set_active(false);
		vbox->pack_start(*recur_cb, false, false, 0);

		//add alarm autostart check button
		autostart_cb = make_managed<Gtk::CheckButton>(_("Auto start when plugin loads"));
		autostart_cb->set_active(false);
		vbox->pack_start(*autostart_cb, false, false, 0);

		auto *bbox = make_managed<Gtk::HButtonBox>();
		bbox->set_spacing(6);
		bbox->set_layout(Gtk::BUTTONBOX_END);
		box->pack_start(*bbox, true, true, 0);

		auto *button_cancel = make_managed<Gtk::Button>(_("Cancel"));
		hbox->pack_start(*button_cancel, true, true, 0);
		button_cancel->signal_clicked().connect([=]{ close(); });

		auto *button_accept = make_managed<Gtk::Button>(_("Accept"));
		hbox->pack_start(*button_accept, true, true, 0);
		button_accept->signal_clicked().connect([=]{ !alrm ?ok_add() :ok_edit(alrm); close(); });

		/* If this is the add window, we're done */
		if (!alrm)
		{
			set_title(_("Add new alarm"));
			alarmdialog_alarmtime_toggled(*rb2);
			show_all();
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
			timem->set_value(time / 60 % 60);
			times->set_value(time % 60);
			alarmdialog_alarmtime_toggled(*rb2);
			rb1->set_active(true);
		}
		else
		{
			time_h->set_value(time / 3600);
			time_m->set_value(time / 60 % 60);
			rb2->set_active(true); // active by default
		}

		set_title(_("Edit alarm"));
		show_all();
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
	panel_plugin.block_menu();

	Gtk::Dialog *dlg = panel_plugin.titled_dialog(_("Xfce4 Timer Options"), _("Close"), GTK_RESPONSE_OK);
	// TODO: see if necessary: register_self_managed(dlg);
	dlg->set_icon_name("xfce4-timer-plugin");

	auto *dialog_vbox = dlg->get_content_area();

	auto *vbox = make_managed<Gtk::VBox>(false, 6);
	dialog_vbox->pack_start(*vbox, true, true, 0);

	dlg->signal_response().connect([=](int response_id){ options_dialog_response(dlg, response_id); });

	vbox->set_border_width(10);

	dlg->set_size_request(650, -1);
	dlg->set_position(Gtk::WIN_POS_CENTER);

	auto *hbox = make_managed<Gtk::HBox>(false, 12);
	vbox->pack_start (*hbox, true, true, 0);

	auto *sw = make_managed<Gtk::ScrolledWindow>();

	sw->set_shadow_type (Gtk::SHADOW_ETCHED_IN);
	sw->set_policy (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

	hbox->pack_start (*sw, true, true, 0);

	liststore->clear();
	for (const auto &alrm: alarm_list)
		set_row(liststore->append(), alrm.get());


	tree = make_managed<Gtk::TreeView>(liststore);
	tree->get_selection()->set_mode(Gtk::SELECTION_SINGLE);

	tree->append_column(_("Timer name"), columns.name);
	tree->append_column(_("Countdown period /\nAlarm time"), columns.info);
	tree->append_column(_("Alarm command"), columns.command);

	sw->add(*tree);
	sw->set_size_request(350, 200);

	auto select = tree->get_selection();
	select->set_mode(Gtk::SELECTION_SINGLE);
	select->signal_changed().connect([=]{ tree_selected(true); });

	auto *buttonbox = make_managed<Gtk::VButtonBox>();
	buttonbox->set_layout(Gtk::BUTTONBOX_START);
	buttonbox->set_spacing(6);
	hbox->pack_start(*buttonbox, false, false, 0);

	buttonadd = make_managed<Gtk::Button>(_("Add"));
	buttonbox->pack_start(*buttonadd, false, false, 0);
	buttonadd->set_sensitive(true);
	buttonadd->signal_clicked().connect([=]{ add_clicked(*dlg); });

	buttonedit = make_managed<Gtk::Button>(_("Edit"));
	buttonbox->pack_start(*buttonedit, false, false, 0);
	buttonedit->set_sensitive(false);
	buttonedit->signal_clicked().connect([=]{ edit_clicked(*dlg); });

	buttonremove = make_managed<Gtk::Button>(_("Remove"));
	buttonbox->pack_start(*buttonremove, false, false, WIDGET_SPACING);
	buttonremove->set_sensitive(false);
	buttonremove->signal_clicked().connect([=]{ up_down_remove_clicked(0); });

	buttonup = make_managed<Gtk::Button>(_("Up"));
	buttonbox->pack_start(*buttonup, false, false, WIDGET_SPACING);
	buttonup->set_sensitive(false);
	buttonup->signal_clicked().connect([=]{ up_down_remove_clicked(-1); });

	buttondown = make_managed<Gtk::Button>(_("Down"));
	buttonbox->pack_start(*buttondown, false, false, WIDGET_SPACING);
	buttondown->set_sensitive(false);
	buttondown->signal_clicked().connect([=]{ up_down_remove_clicked(1); });

	hbox->set_size_request(-1, -1);

	vbox->pack_start(*make_managed<Gtk::Separator>(), false, false, BORDER);

	auto *cbtn_warn = make_managed<Gtk::CheckButton>(_("Don't display a warning  if an alarm command is set"));
	cbtn_warn->set_active(nowin_if_alarm);
	cbtn_warn->signal_toggled().connect([=]{ nowin_if_alarm = cbtn_warn->get_active(); });
	vbox->pack_start(*cbtn_warn, false, false, WIDGET_SPACING);

	vbox->pack_start(*make_managed<Gtk::Separator>(), false, false, BORDER);

	/* Default alarm command config */
	auto *cbtn_default_cmd = make_managed<Gtk::CheckButton>(_("Use a default alarm command"));
	cbtn_default_cmd->set_active(use_global_command);
	cbtn_default_cmd->signal_toggled().connect([=]{ global_command_box->set_sensitive(use_global_command = cbtn_default_cmd->get_active()); });
	vbox->pack_start(*cbtn_default_cmd, false, false, WIDGET_SPACING);

	global_command_box = make_managed<Gtk::HBox>();
	global_command_box->pack_start(*make_managed<Gtk::Label>(_("Default command: ")), false, false, 0);
	global_command_box->set_margin_start (12);
	glob_command_entry = make_managed<Gtk::Entry>();
	glob_command_entry->set_size_request(400, -1);
	glob_command_entry->set_text(global_command);
	global_command_box->pack_start(*glob_command_entry, false, false, 10);

	vbox->pack_start(*global_command_box, false, false, WIDGET_SPACING);
	global_command_box->set_sensitive(use_global_command);

	vbox->pack_start(*make_managed<Gtk::Separator>(), false, false, BORDER);

	/* Alarm repetitions config */
	auto *cbtn_repeat = make_managed<Gtk::CheckButton>(_("Repeat the alarm command"));
	cbtn_repeat->set_active(repeat_alarm_command);
	cbtn_repeat->signal_toggled().connect([=]{ repeat_alarm_box->set_sensitive(repeat_alarm_command = cbtn_repeat->get_active()); });
	vbox->pack_start(*cbtn_repeat, false, false, WIDGET_SPACING);

	repeat_alarm_box = make_managed<Gtk::HBox>();
	repeat_alarm_box->set_margin_start(12);

	repeat_alarm_box->pack_start (*make_managed<Gtk::Label>(_("Number of repetitions")), false, false, 0);
	spin_repeat = make_managed<SpinButtonInt>(1, 50);
	spin_repeat->set_value(repetitions);
	spin_repeat->signal_value_changed().connect([=]{ repetitions = spin_repeat->get_value_as_int(); });
	repeat_alarm_box->pack_start(*spin_repeat, false, false, 10);

	repeat_alarm_box->pack_start(*make_managed<Gtk::Label>(_("  Time interval (sec.)")), false, false, 0);
	spin_interval = make_managed<SpinButtonInt>(1, 600);
	repeat_alarm_box->pack_start(*spin_interval, false, false, 10);
	spin_interval->set_value(repeat_interval);
	spin_interval->signal_value_changed().connect([=]{ repeat_interval = spin_interval->get_value_as_int(); });

	vbox->pack_start(*repeat_alarm_box, false, false, WIDGET_SPACING);
	repeat_alarm_box->set_sensitive(repeat_alarm_command);

	dlg->set_modal(true);
	dlg->show_all();
}

void Plugin::options_dialog_response(Gtk::Dialog *dlg, int reponse_id)
{
	global_command = glob_command_entry->get_text();
	dlg->close();
	panel_plugin.unblock_menu();
	save_settings();
}

void Plugin::save_settings()
{
	auto path = panel_plugin.get_save_location();
	if (path.empty())
		return;

	/**
	 * We do this to start a fresh config file, otherwise if the old config file
	 * is longer,   the tail will not get truncated
	 * See http://bugzilla.xfce.org/show_bug.cgi?id=2647
	 * for a related bug report
	 **/
	{ std::ofstream(file); }

	auto rc = Xfce::Rc(path, false);
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
	auto path = panel_plugin.get_save_location();
	if (path.empty())
		return;

	auto rc = Xfce::Rc(path, true);
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
		nowin_if_alarm       = rc.read("nowin_if_alarm",     false);
		use_global_command   = rc.read("use_global_command", false);
		global_command       = rc.read("global_command",     "");
		repeat_alarm_command = rc.read("repeat_alarm",       false);
		repetitions          = rc.read("repetitions",        1);
		repeat_interval      = rc.read("repeat_interval",    10);
	}

	update_pbar_orientation();
}

void Plugin::add_clicked(Gtk::Window& toplevel) {
	auto *dlg = make_self_managed<AddEditDialog>(toplevel, nullptr);
	dlg->signal_response().connect([=](int id){ add_alarm(std::move(dlg->newalarm)); });
}

void Plugin::edit_clicked(Gtk::Window& toplevel) {
	auto *dlg = make_self_managed<AddEditDialog>(toplevel, selected_row()->get_value(columns.alrm));
	dlg->signal_response().connect([=](int id){ update_selected(); });
}

void Plugin::up_down_remove_clicked(int delta) {
	auto select = selected_row();
	assert(select);

	Alarm *alrm = selected_row()->get_value(columns.alrm);
	int idx = get_alarm_index(alrm);
	if (delta == 0) {
		// remove
		alarm_list.erase(alarm_list.begin() + idx);
		liststore->erase(select);
	} else {
		// up or down
		int idx_other = idx + delta;
		if (!(0<=idx_other && idx_other<alarm_list.size()))
			return;

		std::swap(alarm_list[idx], alarm_list[idx_other]);
		liststore->iter_swap(select, std::next(select, delta));
	}
}

bool Plugin::pbar_clicked(GdkEventButton *btn) {
	if (btn->button != 1)
		return false;
	menu = std::make_unique<AlarmMenu>(signal_update, alarm_list);
	menu->show_all();
	menu->popup_at_widget(pbar, Gdk::GRAVITY_SOUTH_WEST, Gdk::GRAVITY_NORTH_WEST, nullptr);
	menu->signal_alarm_change.connect([=] { enable_heartbeat(); });
	menu->signal_settings.connect([=] { create_options(); });
	return true;
}

int Alarm::remain() const {
	if (is_countdown)
		return time - static_cast<int>(timer.elapsed());

	auto date = Glib::DateTime::create_now_local();
	int current = date.get_hour()*3600 + date.get_minute()*60 + date.get_second();
	int start = current - static_cast<int>(timer.elapsed());
	int full_duration = ((time - start) % DAY_SECS + DAY_SECS) % DAY_SECS;
	return full_duration - (current - start);
}

AlarmMenu::AlarmMenu(sigc::signal<void()>& signal_update, decltype(alarms)& alarms_): alarms(alarms_) {
	for (const auto& alrm: alarms) {
		auto* item_name  = Gtk::make_managed<Gtk::MenuItem>();
		auto* item_pause = Gtk::make_managed<Gtk::MenuItem>();
		auto* item_stop  = Gtk::make_managed<Gtk::MenuItem>(_("Stop timer"));

		append(*item_name);
		append(*item_pause);
		append(*item_stop);
		append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());

		item_name ->signal_activate().connect([=,p=alrm.get()]{ p->start();  signal_alarm_change.emit(); });
		item_pause->signal_activate().connect([=,p=alrm.get()]{ p->toggle(); signal_alarm_change.emit(); });
		item_stop ->signal_activate().connect([=,p=alrm.get()]{ p->stop();   signal_alarm_change.emit(); });

		signal_update_items.connect([=, p = alrm.get()] {
			item_name->set_label(p->get_tag());
			if (p->state != Alarm::OFF) {
				item_name->set_sensitive(false);
				item_pause->set_label(p->state == Alarm::ON ? _("Pause timer") : _("Resume timer"));
				item_pause->set_sensitive(p->is_countdown);
				item_pause->show();
				item_stop->show();
			} else {
				item_name->set_sensitive(true);
				item_pause->hide();
				item_stop->hide();
			}
		});
	}
	update_receiver = signal_update.connect([=] { signal_update_items.emit(); });
	
	auto *item_settings = Gtk::make_managed<Gtk::MenuItem>(_("Edit alarm"));
	append(*item_settings);
	item_settings->signal_activate().connect([=]{ signal_settings.emit(); });

	signal_update_items.emit();
}
