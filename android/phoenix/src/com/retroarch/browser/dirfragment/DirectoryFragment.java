package com.retroarch.browser.dirfragment;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.graphics.Point;
import android.os.Bundle;
import android.os.Environment;
import android.os.Parcel;
import android.os.Parcelable;
import android.support.v4.app.DialogFragment;
import android.view.Display;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ListView;
import android.widget.Toast;

import com.retroarchlite.R;
import com.retroarch.browser.FileWrapper;
import com.retroarch.browser.IconAdapter;
import com.retroarch.browser.NativeInterface;
import com.retroarch.browser.preferences.util.ConfigFile;
import com.retroarch.browser.preferences.util.UserPreferences;

import java.util.*;
import java.io.*;


/**
 * {@link DialogFragment} subclass that provides a file-browser
 * like UI for browsing for specific files.
 * <p>
 * This file browser also allows for custom filtering
 * depending on the type of class that inherits it.
 * <p>
 * This file browser also uses an implementation of a 
 * backstack for remembering previously browsed folders
 * within this DirectoryFragment.
 * <p>
 * To instantiate a new instance of this class
 * you must use the {@code newInstance} method.
 */
public class DirectoryFragment extends DialogFragment
{
   protected IconAdapter<FileWrapper> adapter;
   protected File listedDirectory;
   
   public static ConfigFile mameListFile = null;
   protected static SharedPreferences Prefs = null;
         
   public static String MameName(String filename)
   {
      String lowercase = filename.toLowerCase();
      if ( Prefs.getBoolean("mame_titles", false)
              && mameListFile.keyExists(lowercase) )
         return mameListFile.getString(lowercase);
      else
         return filename;
   }

   public static final class BackStackItem implements Parcelable
   {
      protected final String path;
      protected boolean parentIsBack;

      public BackStackItem(String path, boolean parentIsBack)
      {
         this.path = path;
         this.parentIsBack = parentIsBack;
      }

      private BackStackItem(Parcel in)
      {
         this.path = in.readString();
         this.parentIsBack = in.readInt() != 0;
      }

      public int describeContents()
      {
         return 0;
      }

      public void writeToParcel(Parcel out, int flags)
      {
         out.writeString(path);
         out.writeInt(parentIsBack ? 1 : 0);
      }

      public static final Parcelable.Creator<BackStackItem> CREATOR = new Parcelable.Creator<BackStackItem>()
      {
         public BackStackItem createFromParcel(Parcel in)
         {
            return new BackStackItem(in);
         }

         public BackStackItem[] newArray(int size)
         {
            return new BackStackItem[size];
         }
      };
   }

   /**
    * Listener interface for executing content or performing
    * other things upon the DirectoryFragment instance closing.
    */
   public interface OnDirectoryFragmentClosedListener
   {
      /**
       * Performs some arbitrary action after the 
       * {@link DirectoryFragment} closes.
       * 
       * @param path The path to the file chosen within the {@link DirectoryFragment}
       */
      void onDirectoryFragmentClosed(String path);
   }


   protected ArrayList<BackStackItem> backStack;
   protected String startDirectory;
   protected String pathSettingKey;
   protected boolean isDirectoryTarget;
   protected OnDirectoryFragmentClosedListener onClosedListener;

   /**
    * Sets the starting directory for this DirectoryFragment
    * when it is shown to the user.
    * 
    * @param path the initial directory to show to the user
    *             when this DirectoryFragment is shown.
    */
   public void setStartDirectory(String path)
   {
      startDirectory = path;
   }

   /**
    * Sets the key to save the selected item in the DialogFragment
    * into the application SharedPreferences at.
    * 
    * @param key the key to save the selected item's path to in
    *            the application's SharedPreferences.
    */
   public void setPathSettingKey(String key)
   {
      pathSettingKey = key;
   }

   /**
    * Sets whether or not we are browsing for a specific
    * directory or not. If enabled, it will allow the user
    * to select a specific directory, rather than a file.
    * 
    * @param enable Whether or not to enable this.
    */
   public void setIsDirectoryTarget(boolean enable)
   {
      isDirectoryTarget = enable;
   }

   /**
    * Sets the listener for an action to perform upon the
    * closing of this DirectoryFragment.
    * 
    * @param onClosedListener the OnDirectoryFragmentClosedListener to set.
    */
   public void setOnDirectoryFragmentClosedListener(OnDirectoryFragmentClosedListener onClosedListener)
   {
      this.onClosedListener = onClosedListener;
   }

   /**
    * Retrieves a new instance of a DirectoryFragment
    * with a title specified by the given resource ID.
    * 
    * @param titleResId String resource ID for the title
    *                   of this DirectoryFragment.
    * 
    * @return A new instance of a DirectoryFragment.
    */
   public static DirectoryFragment newInstance(int titleResId)
   {
      final DirectoryFragment dFrag = new DirectoryFragment();
      final Bundle bundle = new Bundle();
      bundle.putInt("titleResId", titleResId);
      dFrag.setArguments(bundle);

      return dFrag;
   }
   
   /**
    * Retrieves a new instance of a DirectoryFragment
    * with a title specified by the given string.
    * 
    * @param title String for the title of this DirectoryFragment.
    * 
    * @return A new instance of a DirectoryFragment.
    */
   public static DirectoryFragment newInstance(String title)
   {
      final DirectoryFragment dFrag = new DirectoryFragment();
      final Bundle bundle = new Bundle();
      bundle.putInt("titleResId", -88);
      bundle.putString("title", title);
      dFrag.setArguments(bundle);

      return dFrag;
   }
   
   public static DirectoryFragment newInstance(String title, List<String> exts)
   {
      final DirectoryFragment dFrag = new DirectoryFragment();
      final Bundle bundle = new Bundle();
      bundle.putInt("titleResId", -88);
      bundle.putString("title", title);
      
      String ext_array[] = new String[exts.size()];
      int i = 0;
      for ( String ext : exts)
      {
         if (ext == null)
            return null;
         ext_array[i++] = ext.toLowerCase();
      }
      bundle.putStringArray("exts", ext_array);
      
      dFrag.setArguments(bundle);

      return dFrag;
   }


   @Override
   public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState)
   {
      ListView rootView = (ListView) inflater.inflate(R.layout.line_list, container, false);
      rootView.setOnItemClickListener(onItemClickListener);
      
      // Set the dialog title.
      if ( getArguments().getInt("titleResId") == -88 )
         getDialog().setTitle(getArguments().getString("title"));
      else
         getDialog().setTitle(getArguments().getInt("titleResId"));
      
      // Set supported extensions
      if (!isDirectoryTarget)
      {
         String ext_array[] = getArguments().getStringArray("exts");
         if ( ext_array != null && ext_array.length > 0 )
            addAllowedExts(ext_array);
         else
            addDisallowedExts("state", "srm", "state.auto", "rtc", "ccd");
      }
      else if (pathSettingKey != null && !pathSettingKey.isEmpty())
      {
         Toast.makeText( getActivity(),
                         "Current Directory:\n"
                         + UserPreferences.getPreferences(getActivity())
                           .getString(pathSettingKey, "<Default>"),
                         Toast.LENGTH_LONG ).show();
      }
      
      // Read mamelist.txt
      if (mameListFile == null)
      {
         String mameListPath = getContext().getApplicationInfo().dataDir
                               + "/info/mamelist.txt";
         mameListFile = new ConfigFile(mameListPath);
         Prefs = UserPreferences.getPreferences(getContext());
      }
      
      // Setup the list
      adapter = new IconAdapter<FileWrapper>(getActivity(), R.layout.line_list_item);
      rootView.setAdapter(adapter);

      // Load Directory
      if (savedInstanceState != null)
      {
         backStack = savedInstanceState.getParcelableArrayList("BACKSTACK");
      }

      if (backStack == null || backStack.isEmpty())
      {
         backStack = new ArrayList<BackStackItem>();
         String startPath = (startDirectory == null || startDirectory.isEmpty()) ?
            Environment.getExternalStorageDirectory().getPath() : startDirectory;
         
         if (pathSettingKey != null && (pathSettingKey.equals("overlay_zip")
                                        || pathSettingKey.equals("shader_zip")
                                        || pathSettingKey.equals("themes_zip")))
            startPath = UserPreferences.getPreferences(getActivity())
                        .getString( "user_zip_directory", startPath);
         
         backStack.add(new BackStackItem(startPath, false));
      }

      wrapFiles();
      return rootView;
   }
   
   @Override
   public void onResume()
   {
      super.onResume();
      final Display display = getActivity().getWindowManager().getDefaultDisplay();
      Point size = new Point();
      display.getSize(size);

      ViewGroup.LayoutParams params = getDialog().getWindow().getAttributes();
      if (size.x > size.y)
         params.width = (65 * size.x) / 100;
      else
         params.width = (85 * size.x) / 100;

      getDialog().getWindow().setAttributes((android.view.WindowManager.LayoutParams) params);
   }

   private final OnItemClickListener onItemClickListener = new OnItemClickListener()
   {
      @Override
      public void onItemClick(AdapterView<?> parent, View view, int position, long id)
      {
         final FileWrapper item = adapter.getItem(position);

         if (item.isParentItem() && backStack.get(backStack.size() - 1).parentIsBack)
         {
            backStack.remove(backStack.size() - 1);
            wrapFiles();
            return;
         }
         else if (item.isDirSelectItem())
         {
            finishWithPath(listedDirectory.getAbsolutePath());
            Toast.makeText(getActivity(), "Selected Directory:\n" + listedDirectory.getAbsolutePath(), Toast.LENGTH_LONG).show();
            return;
         }

         final File selected = item.isParentItem() ? listedDirectory.getParentFile() : item.getFile();

         if (selected.isDirectory())
         {
            backStack.add(new BackStackItem(selected.getAbsolutePath(), !item.isParentItem()));
            wrapFiles();
         }
         else
         {
            String filePath = selected.getAbsolutePath();
            finishWithPath(filePath);
         }
      }
   };

   @Override
   public void onSaveInstanceState(Bundle outState)
   {
      super.onSaveInstanceState(outState);

      outState.putParcelableArrayList("BACKSTACK", backStack);
   }

   private void finishWithPath(String path)
   {
      if (onClosedListener != null)
      {
         onClosedListener.onDirectoryFragmentClosed(path);
      }
      else if (pathSettingKey != null && !pathSettingKey.isEmpty())
      {
         if ( pathSettingKey.equals("shader_zip"))
            ExtractZipWithPrompt(path,
                    getContext().getApplicationInfo().dataDir + "/shaders_glsl",
                    "shaders");
         else if ( pathSettingKey.equals("overlay_zip"))
            ExtractZipWithPrompt(path,
                    getContext().getApplicationInfo().dataDir + "/overlays",
                    "overlays");
         else if ( pathSettingKey.equals("themes_zip"))
            ExtractZipWithPrompt(path,
                    getContext().getApplicationInfo().dataDir + "/themes_rgui",
                    "themes");
         else
         {
            SharedPreferences settings = UserPreferences.getPreferences(getActivity());
            SharedPreferences.Editor editor = settings.edit();
            editor.putString(pathSettingKey, path).apply();
         }
      }

      dismiss();
   }

   // TODO: Hook this up to a callable interface (if backstack is desirable).
   public boolean onKeyDown(int keyCode, KeyEvent event)
   {
      if (keyCode == KeyEvent.KEYCODE_BACK)
      {
         if (backStack.size() > 1)
         {
            backStack.remove(backStack.size() - 1);
            wrapFiles();
         }

         return true;
      }

      return false;
   }

   private ArrayList<String> allowedExt;
   private ArrayList<String> disallowedExt;

   private boolean filterPath(String path)
   {
      path = path.toLowerCase();
      
      if (disallowedExt != null)
      {
         for (String ext : disallowedExt)
         {
            if (path.endsWith(ext))
               return false;
         }
      }
      
      if (allowedExt != null)
      {
         for (String ext : allowedExt)
         {
            if (path.endsWith(ext))
               return true;
         }
         
         return false;
      }

      return true;
   }

   /**
    * Allows specifying an allowed file extension.
    * <p>
    * Any files that contain this file extension will be shown
    * within the DirectoryFragment file browser. Those that don't
    * contain this extension will not be shows.
    * <p>
    * It is possible to specify more than one allowed extension by
    * simply calling this method with a different file extension specified.
    * 
    * @param exts The file extension(s) to allow being shown in this DirectoryFragment.
    */
   public void addAllowedExts(String... exts)
   {
      if (allowedExt == null)
         allowedExt = new ArrayList<String>();

      allowedExt.addAll(Arrays.asList(exts));
   }

   /**
    * Allows specifying a disallowed file extension.
    * <p>
    * Any files that contain this file extension will not be shown
    * within the DirectoryFragment file browser.
    * <p>
    * It is possible to specify more than one disallowed extension by
    * simply calling this method with a different file extension specified.
    * 
    * @param exts The file extension(s) to hide from being shown in this DirectoryFragment.
    */
   public void addDisallowedExts(String... exts)
   {
      if (disallowedExt == null)
         disallowedExt = new ArrayList<String>();

      disallowedExt.addAll(Arrays.asList(exts));
   }

   protected void wrapFiles()
   {
      listedDirectory = new File(backStack.get(backStack.size() - 1).path);

      if (!listedDirectory.isDirectory())
      {
         throw new IllegalArgumentException("Directory is not valid.");
      }

      adapter.clear();
      
      if (isDirectoryTarget)
         adapter.add(new FileWrapper(null, FileWrapper.DIRSELECT, true));

      if (listedDirectory.getParentFile() != null)
         adapter.add(new FileWrapper(null, FileWrapper.PARENT, true));

      // Copy new items
      final File[] files = listedDirectory.listFiles();
      if (files != null)
      {
         for (File file : files)
         {
            String path = file.getName();

            boolean allowFile = file.isDirectory() || (filterPath(path) && !isDirectoryTarget);
            if (allowFile)
            {
               adapter.add(new FileWrapper(file, FileWrapper.FILE, true));
            }
         }
         
         if (allowedExt != null)
            if (allowedExt.contains("cue") && allowedExt.contains("bin"))
               PruneBinEntries();
      }

      // Sort items
      adapter.sort(new Comparator<FileWrapper>()
      {
         @Override
         public int compare(FileWrapper left, FileWrapper right)
         {
            return left.compareTo(right);
         };
      });
      
      // Update
      adapter.notifyDataSetChanged();
   }
   
   // Remove .bin entries where same-name .cue entries exist
   void PruneBinEntries()
   {
      // Even if sorted, can't assume .cue is next entry after .bin
      for (int i = 0; i < adapter.getCount(); i++)
      {
         String name = adapter.getItem(i).getText().toLowerCase();
         if ( name.endsWith(".bin") )
         {
            for (int j = 0; j < adapter.getCount(); j++)
            {
               String other_name = adapter.getItem(j).getText().toLowerCase();
               if (j == i || !other_name.endsWith(".cue"))
                  continue;
               if ( other_name.replace(".cue", ".bin")
                    .equals(name) )
               {
                  adapter.remove(adapter.getItem(i--));
                  break;
               }
            }
         }
      }
   }
    
   public boolean ExtractZipWithPrompt(final String zipPath,
                                       final String destDir,
                                       final String quickDesc)
   {
      final Toast successToast = Toast.makeText(getContext(), "Zip contents extracted.", Toast.LENGTH_SHORT);

      AlertDialog.Builder builder = new AlertDialog.Builder(getContext());
      builder.setMessage("Confirm: Extract " + quickDesc + " from " + zipPath.substring(zipPath.lastIndexOf('/')+1) + "?")
         .setCancelable(true)
         .setPositiveButton("Yes",
               new DialogInterface.OnClickListener()
               {
                  public void onClick(DialogInterface dialog, int id)
                  {
                     try
                     {
                        boolean success = NativeInterface.extractArchiveTo(zipPath, null, destDir);
                        if (!success)
                           throw new IOException("Failed to extract files ...");
                        else
                           successToast.show();
                     }
                     catch (IOException e) {}
                  }
               })
         .setNegativeButton("No", new DialogInterface.OnClickListener()
         {
            public void onClick(DialogInterface dialog, int id) {}
         });
      Dialog dialog = builder.create();
      dialog.show();
      
      SharedPreferences prefs = UserPreferences.getPreferences(getContext());
      prefs.edit().putString("user_zip_directory",
                             zipPath.substring(0, zipPath.lastIndexOf( "/" )))
                            .commit();
      return true;
   }

   
   public boolean RestoreDirFromZip(final String zipPath,
                                    final String zipSubDir,
                                    final String destDir)
   {
      boolean success = false;
      try
      {
         File dir = new File(destDir); 
         if (dir.isDirectory()) 
         {
            String[] names = dir.list();
            for (String name : names)
            {
               File f = new File(dir, name);
               DirectoryFragment.DeleteDirTree(f);
            }
         }

         success = NativeInterface.extractArchiveTo(zipPath, zipSubDir, destDir);
         if (!success) {
            throw new IOException("Failed to extract assets ...");
         }
      }
      catch (IOException e)
      {success = false;}
      
      return success;
   }
   
   public static boolean DeleteDirTree(File topDir)
   {
      if ( topDir.isDirectory() )
      {
         String[] names = topDir.list();
         for (String name : names)
         {
            File f = new File(topDir, name);
            DeleteDirTree(f);
         }
      }
      
      return topDir.delete();
   }
}
