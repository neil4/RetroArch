package com.retroarch.browser.preferences.fragments;

import android.os.Bundle;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceClickListener;

import com.retroarch.browser.dirfragment.DirectoryFragment;
import com.retroarch.browser.preferences.fragments.util.PreferenceListFragment;
import com.retroarchlite.BuildConfig;
import com.retroarchlite.R;

/**
 * A {@link PreferenceListFragment} that handles the path preferences.
 */
public final class PathPreferenceFragment extends PreferenceListFragment implements OnPreferenceClickListener
{
   @Override
   public void onCreate(Bundle savedInstanceState)
   {
      super.onCreate(savedInstanceState);

      // Add path preferences from the XML.
      if (BuildConfig.APPLICATION_ID.contains("64"))
         addPreferencesFromResource(R.xml.path_preferences_64);
      else
         addPreferencesFromResource(R.xml.path_preferences_32);

      // Set preference click listeners
      findPreference("romDirPref").setOnPreferenceClickListener(this);
      findPreference("srmDirPref").setOnPreferenceClickListener(this);
      findPreference("saveStateDirPref").setOnPreferenceClickListener(this);
      findPreference("systemDirPref").setOnPreferenceClickListener(this);
      findPreference("configDirPref").setOnPreferenceClickListener(this);
      findPreference("backupCoresDirPref").setOnPreferenceClickListener(this);
   }
   
   @Override
   public boolean onPreferenceClick(Preference preference)
   {
      final String prefKey = preference.getKey();

      // Custom ROM directory
      if (prefKey.equals("romDirPref"))
      {
         final DirectoryFragment romDirBrowser = DirectoryFragment.newInstance(R.string.rom_directory_select);
         romDirBrowser.setPathSettingKey("rgui_browser_directory");
         romDirBrowser.setIsDirectoryTarget(true);
         romDirBrowser.show(getFragmentManager(), "romDirBrowser");
      }
      // Custom savefile directory
      else if (prefKey.equals("srmDirPref"))
      {
         final DirectoryFragment srmDirBrowser = DirectoryFragment.newInstance(R.string.savefile_directory_select);
         srmDirBrowser.setPathSettingKey("savefile_directory");
         srmDirBrowser.setIsDirectoryTarget(true);
         srmDirBrowser.show(getFragmentManager(), "srmDirBrowser");
      }
      // Custom save state directory
      else if (prefKey.equals("saveStateDirPref"))
      {
         final DirectoryFragment saveStateDirBrowser = DirectoryFragment.newInstance(R.string.save_state_directory_select);
         saveStateDirBrowser.setPathSettingKey("savestate_directory");
         saveStateDirBrowser.setIsDirectoryTarget(true);
         saveStateDirBrowser.show(getFragmentManager(), "saveStateDirBrowser");
      }
      // Custom system directory
      else if (prefKey.equals("systemDirPref"))
      {
         final DirectoryFragment systemDirBrowser = DirectoryFragment.newInstance(R.string.system_directory_select);
         systemDirBrowser.setPathSettingKey("system_directory");
         systemDirBrowser.setIsDirectoryTarget(true);
         systemDirBrowser.show(getFragmentManager(), "systemDirBrowser");
      }
      // Custom config directory
      else if (prefKey.equals("configDirPref"))
      {
         final DirectoryFragment configDirBrowser = DirectoryFragment.newInstance(R.string.config_directory_select);
         configDirBrowser.setPathSettingKey("rgui_config_directory");
         configDirBrowser.setIsDirectoryTarget(true);
         configDirBrowser.show(getFragmentManager(), "configDirBrowser");
      }
      // Local Installable Cores directory
      else if (prefKey.equals("backupCoresDirPref"))
      {
         final DirectoryFragment coreDirBrowser = DirectoryFragment.newInstance(R.string.backup_cores_directory_select);
         coreDirBrowser.setPathSettingKey("backup_cores_directory");
         coreDirBrowser.setIsDirectoryTarget(true);
         coreDirBrowser.show(getFragmentManager(), "coreDirBrowser");
      }

      return true;
   }
}
