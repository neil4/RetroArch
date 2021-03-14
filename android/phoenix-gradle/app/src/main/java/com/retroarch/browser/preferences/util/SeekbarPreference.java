package com.retroarch.browser.preferences.util;

import android.content.Context;
import android.preference.DialogPreference;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.SeekBar;
import android.widget.TextView;

import com.retroarchlite.R;

public final class SeekbarPreference extends DialogPreference implements SeekBar.OnSeekBarChangeListener {

   private float seekValue;
   private SeekBar bar;
   private TextView text;
   private final Context context;

   public SeekbarPreference(Context context, AttributeSet attrs) {
      super(context, attrs);

      this.context = context;
   }


   @Override
   protected View onCreateDialogView()
   {
      LayoutInflater inflater = LayoutInflater.from(context);
      View view = inflater.inflate(R.layout.seek_dialog, null);
      this.bar = (SeekBar) view.findViewById(R.id.seekbar_bar);
      this.text = (TextView) view.findViewById(R.id.seekbar_text);
      this.seekValue = getPersistedFloat(1.0f);

      // Set initial progress for seek bar and set the listener.
      int prog = (int) (seekValue * 100);
      this.bar.setProgress(prog);
      this.text.setText(prog + "%");
      this.bar.setOnSeekBarChangeListener(this);

      return view;
   }

   @Override
   protected void onDialogClosed(boolean positiveResult) {
      super.onDialogClosed(positiveResult);

      if (positiveResult) {
         persistFloat(seekValue);
      }
   }

   @Override
   public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
      seekValue = (float) progress / 100.0f;
      text.setText((int) (seekValue * 100) + "%");
   }

   @Override
   public void onStartTrackingTouch(SeekBar seekBar) {
   }

   @Override
   public void onStopTrackingTouch(SeekBar seekBar) {
   }
}
