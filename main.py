#!/usr/bin/env python3
import sys
import os
import subprocess
import threading
import glob
import json
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Gtk, Adw, Gio, GLib

def get_config_file_path():
    config_dir = os.path.join(GLib.get_user_config_dir(), 'rtspeed')
    return os.path.join(config_dir, 'config.json')

def load_config():
    config_file = get_config_file_path()
    if os.path.exists(config_file):
        try:
            with open(config_file, 'r') as f:
                return json.load(f)
        except Exception as e:
            print("Failed to load config:", e)
    return {}

def save_config(config):
    config_file = get_config_file_path()
    os.makedirs(os.path.dirname(config_file), exist_ok=True)
    try:
        with open(config_file, 'w') as f:
            json.dump(config, f, indent=4)
    except Exception as e:
        print("Failed to save config:", e)

class InverterConfigApp(Adw.Application):
    def __init__(self):
        super().__init__(application_id='com.rtspeed.configurator')
        self.config = load_config()
        self.project_dir = self.config.get("project_dir", "")

    def do_activate(self):
        window = Adw.ApplicationWindow(application=self, title="RTSpeed Configurator")
        window.set_default_size(800, 600)

        # Toolbar View
        toolbar_view = Adw.ToolbarView()
        window.set_content(toolbar_view)

        # Header Bar
        header_bar = Adw.HeaderBar()
        toolbar_view.add_top_bar(header_bar)

        # View Stack
        self.view_stack = Adw.ViewStack()
        toolbar_view.set_content(self.view_stack)

        # View Switcher Title
        switcher_title = Adw.ViewSwitcherTitle()
        switcher_title.set_stack(self.view_stack)
        header_bar.set_title_widget(switcher_title)

        self.setup_configure_page()
        self.setup_flash_page()
        self.setup_about_page()

        window.present()

    def setup_configure_page(self):
        page = Adw.PreferencesPage()
        
        # Project Group
        group_proj = Adw.PreferencesGroup(title="STM32CubeIDE Project")
        
        self.row_proj = Adw.ActionRow(title="Project Path", subtitle=self.project_dir if self.project_dir else "Not selected")
        btn_choose = Gtk.Button(label="Select Folder", valign=Gtk.Align.CENTER)
        btn_choose.connect("clicked", self.on_select_folder_clicked)
        self.row_proj.add_suffix(btn_choose)
        group_proj.add(self.row_proj)

        # Build Command
        self.row_build_cmd = Adw.EntryRow(title="Build Command")
        self.row_build_cmd.set_text("make -j4")
        group_proj.add(self.row_build_cmd)

        # Flash Command
        self.row_flash_cmd = Adw.EntryRow(title="Flash Command")
        self.row_flash_cmd.set_text("st-flash --reset write build/*.bin 0x8000000")
        group_proj.add(self.row_flash_cmd)

        page.add(group_proj)

        # Gains Groups
        self.gains = {}
        
        controllers = [
            ("Current Controller (Iq)", "iq"), 
            ("Current Controller (Id)", "id"), 
            ("Speed Controller", "speed")
        ]
        
        for ctrl_name, prefix in controllers:
            group = Adw.PreferencesGroup(title=ctrl_name)
            for gain, default_val in [("P", 1.0), ("I", 0.0), ("D", 0.0)]:
                spin = Gtk.SpinButton.new_with_range(0.0, 10000.0, 0.01)
                spin.set_value(default_val)
                spin.set_valign(Gtk.Align.CENTER)
                
                row = Adw.ActionRow(title=f"{gain} Gain")
                row.add_suffix(spin)
                group.add(row)
                
                self.gains[f"{prefix}_{gain.lower()}"] = spin
            page.add(group)

        # Presets Group
        group_presets = Adw.PreferencesGroup(title="Presets")
        
        self.preset_model = Gtk.StringList()
        self.combo_presets = Gtk.DropDown(model=self.preset_model, valign=Gtk.Align.CENTER)
        
        row_load = Adw.ActionRow(title="Preset Management")
        row_load.add_suffix(self.combo_presets)
        
        btn_load = Gtk.Button(label="Load", valign=Gtk.Align.CENTER)
        btn_load.connect("clicked", self.on_load_preset)
        row_load.add_suffix(btn_load)
        
        btn_delete = Gtk.Button(label="Delete", valign=Gtk.Align.CENTER)
        btn_delete.add_css_class("destructive-action")
        btn_delete.connect("clicked", self.on_delete_preset)
        row_load.add_suffix(btn_delete)
        
        group_presets.add(row_load)
        
        row_save = Adw.ActionRow(title="Save Current As...")
        self.entry_preset_name = Gtk.Entry(placeholder_text="Preset Name", valign=Gtk.Align.CENTER)
        row_save.add_suffix(self.entry_preset_name)
        
        btn_save = Gtk.Button(label="Save", valign=Gtk.Align.CENTER)
        btn_save.add_css_class("suggested-action")
        btn_save.connect("clicked", self.on_save_preset)
        row_save.add_suffix(btn_save)
        
        group_presets.add(row_save)
        
        page.add(group_presets)
        
        self.presets = self.config.get("presets", {})
        for p in self.presets:
            self.preset_model.append(p)

        self.view_stack.add_titled_with_icon(page, "configure", "Configure", "preferences-system-symbolic")

    def on_load_preset(self, button):
        selected_item = self.combo_presets.get_selected_item()
        if selected_item:
            preset_name = selected_item.get_string()
            if preset_name in self.presets:
                vals = self.presets[preset_name]
                for k, v in vals.items():
                    if k in self.gains:
                        self.gains[k].set_value(float(v))

    def on_delete_preset(self, button):
        selected_item = self.combo_presets.get_selected_item()
        if selected_item:
            preset_name = selected_item.get_string()
            if preset_name in self.presets:
                del self.presets[preset_name]
                self.config["presets"] = self.presets
                save_config(self.config)
                
                # find the index and remove
                for i in range(self.preset_model.get_n_items()):
                    if self.preset_model.get_string(i) == preset_name:
                        self.preset_model.remove(i)
                        break

    def on_save_preset(self, button):
        preset_name = self.entry_preset_name.get_text().strip()
        if not preset_name:
            return
            
        vals = {}
        for k, spin in self.gains.items():
            vals[k] = spin.get_value()
            
        if preset_name not in self.presets:
            self.preset_model.append(preset_name)
            
        self.presets[preset_name] = vals
        self.config["presets"] = self.presets
        save_config(self.config)
        self.entry_preset_name.set_text("")
        
        # Select the newly saved preset
        for i in range(self.preset_model.get_n_items()):
            if self.preset_model.get_string(i) == preset_name:
                self.combo_presets.set_selected(i)
                break

    def on_select_folder_clicked(self, button):
        dialog = Gtk.FileDialog(title="Select STM32CubeIDE Project Folder")
        dialog.select_folder(self.get_active_window(), None, self.on_folder_selected)

    def on_folder_selected(self, dialog, result):
        try:
            folder = dialog.select_folder_finish(result)
            if folder:
                self.project_dir = folder.get_path()
                self.row_proj.set_subtitle(self.project_dir)
                self.config["project_dir"] = self.project_dir
                save_config(self.config)
        except Exception as e:
            print("Error selecting folder:", e)

    def setup_flash_page(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        box.set_margin_top(24)
        box.set_margin_bottom(24)
        box.set_margin_start(24)
        box.set_margin_end(24)

        status = Adw.StatusPage(title="Flash Firmware", 
                                description="Apply current gains, rebuild, and flash to STM32 via ST-Link V2",
                                icon_name="drive-harddisk-symbolic")
        box.append(status)

        self.btn_flash = Gtk.Button(label="Apply and Flash")
        self.btn_flash.set_halign(Gtk.Align.CENTER)
        self.btn_flash.add_css_class("suggested-action")
        self.btn_flash.add_css_class("pill")
        self.btn_flash.connect("clicked", self.on_flash_clicked)
        box.append(self.btn_flash)

        self.term_buffer = Gtk.TextBuffer()
        term_view = Gtk.TextView(buffer=self.term_buffer)
        term_view.set_editable(False)
        term_view.set_monospace(True)
        
        scroll = Gtk.ScrolledWindow()
        scroll.set_child(term_view)
        scroll.set_vexpand(True)
        
        # Add a frame around the text view
        frame = Gtk.Frame()
        frame.set_child(scroll)
        frame.set_vexpand(True)

        box.append(frame)

        self.view_stack.add_titled_with_icon(box, "flash", "Flash", "media-flash-symbolic")

    def on_flash_clicked(self, button):
        if not self.project_dir:
            self.append_log("Error: Please select a project directory in the Configure tab first.\n")
            return
            
        self.btn_flash.set_sensitive(False)
        self.term_buffer.set_text("")
        self.append_log("Generating configuration header...\n")
        
        # Run flashing process in a thread
        threading.Thread(target=self.flash_worker, daemon=True).start()

    def flash_worker(self):
        try:
            # 1. Generate Header
            # We assume a file named "Inc/pid_gains.h"
            inc_dir = os.path.join(self.project_dir, "Core", "Inc")
            if not os.path.exists(inc_dir):
                # Try fallback just Inc
                inc_dir = os.path.join(self.project_dir, "Inc")
                if not os.path.exists(inc_dir):
                    os.makedirs(inc_dir, exist_ok=True)
            
            header_path = os.path.join(inc_dir, "pid_gains.h")
            
            content = f"""#ifndef PID_GAINS_H
#define PID_GAINS_H

// Auto-generated by RTSpeed Configurator

// Iq Controller
#define IQ_KP {self.gains['iq_p'].get_value()}f
#define IQ_KI {self.gains['iq_i'].get_value()}f
#define IQ_KD {self.gains['iq_d'].get_value()}f

// Id Controller
#define ID_KP {self.gains['id_p'].get_value()}f
#define ID_KI {self.gains['id_i'].get_value()}f
#define ID_KD {self.gains['id_d'].get_value()}f

// Speed Controller
#define SPEED_KP {self.gains['speed_p'].get_value()}f
#define SPEED_KI {self.gains['speed_i'].get_value()}f
#define SPEED_KD {self.gains['speed_d'].get_value()}f

#endif // PID_GAINS_H
"""
            with open(header_path, "w") as f:
                f.write(content)
                
            GLib.idle_add(self.append_log, f"Successfully wrote gains to {header_path}\n")
            
            # 2. Build the project
            build_cmd = self.row_build_cmd.get_text()
            GLib.idle_add(self.append_log, f"Running build: {build_cmd}\n")
            
            proc = subprocess.Popen(build_cmd, shell=True, cwd=self.project_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            
            for line in proc.stdout:
                GLib.idle_add(self.append_log, line)
                
            proc.wait()
            if proc.returncode != 0:
                GLib.idle_add(self.append_log, f"Build failed with return code {proc.returncode}\n")
                GLib.idle_add(self.finish_flash)
                return
                
            GLib.idle_add(self.append_log, "Build successful.\nFlashing via ST-Link V2...\n")
            
            # 3. Flash via st-flash
            flash_cmd_template = self.row_flash_cmd.get_text()
            
            # If there's a glob like build/*.bin, we might need to resolve it
            # But wait, shell=True in subprocess.Popen handles globs perfectly!
            
            GLib.idle_add(self.append_log, f"Running flash: {flash_cmd_template}\n")
            proc_flash = subprocess.Popen(flash_cmd_template, shell=True, cwd=self.project_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            for line in proc_flash.stdout:
                GLib.idle_add(self.append_log, line)
                
            proc_flash.wait()
            if proc_flash.returncode == 0:
                GLib.idle_add(self.append_log, "Flash successful!\n")
            else:
                GLib.idle_add(self.append_log, f"Flash failed with code {proc_flash.returncode}\n")
                
        except Exception as e:
            GLib.idle_add(self.append_log, f"Exception occurred: {str(e)}\n")
            
        GLib.idle_add(self.finish_flash)

    def append_log(self, text):
        end_iter = self.term_buffer.get_end_iter()
        self.term_buffer.insert(end_iter, text)
        
        # Scroll to bottom
        # A simple way to scroll to the end
        mark = self.term_buffer.create_mark(None, self.term_buffer.get_end_iter(), False)
        # We need the textview to scroll, but we don't have direct access here easily without storing it.
        # But inserting at end_iter usually auto-scrolls if the cursor is there, or we can just emit a scroll event if we had a reference.

    def finish_flash(self):
        self.btn_flash.set_sensitive(True)

    def setup_about_page(self):
        status = Adw.StatusPage(title="RTSpeed Configurator", 
                                description="Custom inverter configuration and flashing tool.",
                                icon_name="help-about-symbolic")
        
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        box.append(status)
        
        lbl = Gtk.Label(label="This application allows you to:\n1. Configure PID gains for the inverter's controllers\n2. Recompile the STM32 firmware\n3. Flash it to the board using ST-Link v2\n\nBuilt with Python, GTK4 and Libadwaita.")
        lbl.set_justify(Gtk.Justification.CENTER)
        box.append(lbl)
        
        self.view_stack.add_titled_with_icon(box, "about", "About", "help-about-symbolic")

if __name__ == '__main__':
    app = InverterConfigApp()
    sys.exit(app.run(sys.argv))
