package com.retroarch.browser.coremanager.fragments;

/**
 * Represents a core that can be downloaded.
 */
final class DownloadableCore implements Comparable<DownloadableCore>
{
   private final String coreName;
   private final String systemName;
   private final String coreURL;
   private final String shortURL;
   public static boolean sortBySystem;
      
   /**
    * Constructor
    *
    * @param coreName Name of the core.
    * @param systemName Name of the system emulated by the core
    * @param coreURL  URL to this core.
    */
   public DownloadableCore(String coreName, String systemName, String coreURL)
   {
      this.coreName = coreName;
      this.systemName = systemName;
      this.coreURL  = coreURL;
      this.shortURL = coreURL.substring(coreURL.lastIndexOf('/') + 1);
   }

   /**
    * Gets the name of this core.
    *
    * @return The name of this core.
    */
   public String getCoreName()
   {
      return coreName;
   }
   
   public String getSystemName()
   {
      return systemName;
   }

   /**
    * Gets the URL to download this core.
    *
    * @return The URL to download this core.
    */
   public String getCoreURL()
   {
      return coreURL;
   }

   /**
    * Gets the short URL name of this core.
    * <p>
    * e.g. Consider the url: www.somesite/somecore.zip.
    *      This would return "somecore.zip"
    *
    * @return the short URL name of this core.
    */
   public String getShortURLName()
   {
      return shortURL;
   }
   
   @Override
   public int compareTo(DownloadableCore other)
   {
      if ( sortBySystem )
      {
         if( systemName == null || systemName.isEmpty() )
            return 1;
         if( other.systemName == null || other.systemName.isEmpty() )
            return -1;
         else
            return systemName.compareToIgnoreCase(other.systemName);
      }
      else // sort by name
      {
         if( coreName == null || coreName.isEmpty() )
            return 1;
         if( other.coreName == null || other.coreName.isEmpty() )
            return -1;
         else
            return coreName.compareToIgnoreCase(other.coreName);
      }
   }
}
