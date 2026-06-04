//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

package com.ansca.corona;

import android.content.Context;
import android.graphics.Rect;
import android.text.InputFilter;
import android.text.InputType;
import android.text.method.KeyListener;
import android.text.method.PasswordTransformationMethod;
import android.text.method.SingleLineTransformationMethod;
import android.text.method.TextKeyListener;
import android.text.method.TransformationMethod;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.TextView;
import android.util.Log;
import android.widget.Scroller;
import android.text.method.ScrollingMovementMethod;
import android.text.method.HideReturnsTransformationMethod;


public class CoronaEditText extends EditText {
	private static int sBorderPaddingTop = -1;
	private static int sBorderPaddingBottom = -1;
	private static int sBorderPaddingLeft = -1;
	private static int sBorderPaddingRight = -1;
	private boolean myIsPassword = false;
	private int myTextColor = 0;
	private boolean myClearingFocus = false;
	private String myPreviousText = null;
	private String myCurrentText = "";
	private int editingNumDeleted = 0;
	private int editingStart = 0;
	private int editingBefore = 0;
	private int editingAfter = 0;
	private boolean isEditing = false;
	private boolean noEmoji = false;
	private int myMaxLength = 0;
	private TextKeyListener myKeyListener = null;
	private CoronaRuntime myCoronaRuntime = null;

	public CoronaEditText(Context context, CoronaRuntime runtime) {
		super(context);

		myCoronaRuntime = runtime;

		// This fixes an issue on older versions of Android where the default
		// text field padding seems to be in points and which causes text
		// fields on higher resolution content areas (i.e. more than 320x480)
		// to have unreadably small text. The apparently trivial fix seems to
		// work because it sets the padding in pixels and the happy result is
		// that the text appears roughly the same size on all resolutions.
		setPadding(10, 4, 10, 8);

		setOnFocusChangeListener(new OnFocusChangeListener() {
			
			public void onFocusChange(View view, boolean hasFocus) {
				if (myCoronaRuntime != null && myCoronaRuntime.isRunning() && getId() != 0) {
					isEditing = hasFocus;
					myCoronaRuntime.getTaskDispatcher().send(new com.ansca.corona.events.TextTask(getId(), hasFocus, false));
				}
			}
		});
		
		setOnEditorActionListener(new TextView.OnEditorActionListener() {
			
			public boolean onEditorAction(TextView view, int actionId, KeyEvent arg2) {
				// Raise a "submitted" event.
				if (getId() != 0) {
					if (myCoronaRuntime != null && myCoronaRuntime.isRunning()) {
						myCoronaRuntime.getTaskDispatcher().send(new com.ansca.corona.events.TextTask(getId(), false, true));
					}
				}
				
				// Returning false indicates that we are not handling this event.
				return false;
			}
		});

	
		addTextChangedListener(new TextWatcher() {

			public void beforeTextChanged(CharSequence text, int start, int count, int after)
			{
				if ( isEditing )
				{
					myPreviousText = new String( text.toString() );
					editingNumDeleted = count;
				}
			}

			public void afterTextChanged(Editable text)
			{
				myCurrentText = text.toString();
				if (myCurrentText == null) {
					myCurrentText = "";
				}

				if (noEmoji)
				{
					// Remove "emoji" (Unicode "SYMBOL OTHER" group)
					String myFilteredText = myCurrentText.replaceAll("\\p{So}+", "");

					if (! myFilteredText.equals(myCurrentText))
					{
						text.clear();
						text.replace(0, 0, myFilteredText);
						myCurrentText = text.toString();
					}
				}

				if ( isEditing  && (getId() != 0) )
				{
					String newstring = null;

					int numDeleted = 0;
					String newchars = null;

					if( (null != text) )
					{
						newstring = myCurrentText;
						newchars = text.subSequence(editingStart, editingStart+editingAfter).toString();
					}

					if (myCoronaRuntime != null && myCoronaRuntime.isRunning()) {
						myCoronaRuntime.getTaskDispatcher().send(new com.ansca.corona.events.TextEditingTask(
							getId(), editingStart, editingNumDeleted, newchars, myPreviousText, newstring));
					}
				}
			}

			public void onTextChanged( CharSequence text, int start, int before, int after )
			{

				if ( isEditing )
				{
					// This is probably bad and we should be using setSpan() instead.
					// But we cannot find documentation or examples on how to use setSpan().
					// Waiting to hear back from Daniel.
					editingStart = start;
					editingBefore = before;
					editingAfter = after;
				}

			}
		});

		// No autotext. Turns out the false argument here doesn't work, so we need to override getInputType.
		myKeyListener = new TextKeyListener(TextKeyListener.Capitalize.NONE, false) {

			/* (non-Javadoc)
			 * @see android.text.method.TextKeyListener#getInputType()
			 */
			@Override
			public int getInputType() {
				int type = InputType.TYPE_CLASS_TEXT;
				if ( myIsPassword ) {
					type |= InputType.TYPE_TEXT_VARIATION_PASSWORD;
				}
				else {
					type |= InputType.TYPE_TEXT_VARIATION_NORMAL;
				}
				if (myCoronaRuntime != null) {
					Controller controller = myCoronaRuntime.getController();
					if (controller != null) {
						type |= controller.getAndroidVersionSpecific().inputTypeFlagsNoSuggestions();
					}
				}
				return type;
			}
			
		};
		setKeyListener(myKeyListener);

		// There is an issue on certain keyboard which will cause it to dispatch a delete key event on 
		// both key up and key down which causes the text field to delete 2 characters.
		setOnKeyListener(new View.OnKeyListener() {
			@Override
			public boolean onKey(View v, int keyCode, KeyEvent event) {
				if (keyCode == KeyEvent.KEYCODE_DEL) {
					// Letting the CoronaKeyEvent fall through in case we want to do anything special with it
					if (event instanceof com.ansca.corona.input.CoronaKeyEvent) {
						return false;
					}
					v.dispatchKeyEvent(new com.ansca.corona.input.CoronaKeyEvent(event));
					return true;
				}
				//Don't handle any of the non delete keys
				return false;
			}
		});
		
	}

	/* (non-Javadoc)
	 * @see android.view.View#clearFocus()
	 */
	@Override
	public void clearFocus() {
		myClearingFocus = true;

		super.clearFocus();
		
		myClearingFocus = false;
	}

	/* (non-Javadoc)
	 * @see android.view.View#requestFocus(int, android.graphics.Rect)
	 */
	@Override
	public boolean requestFocus(int direction, Rect previouslyFocusedRect) {
		if ( myClearingFocus )
			return false;
		
		return super.requestFocus(direction, previouslyFocusedRect);
	}

	public void setTextViewInputType( String inputType )
	{
		int type = 0;
		
		if ( "number".equals( inputType ) ) {
			type = InputType.TYPE_CLASS_NUMBER;
		} else if ( "phone".equals( inputType ) ) {
			type = InputType.TYPE_CLASS_PHONE;
		} else if ( "url".equals( inputType ) ) {
			type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI;
		} else if ( "email".equals( inputType ) ) {
			type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS;
		} else if ( "decimal".equals( inputType ) ) {
			type = InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL;
		} else if ( "no-emoji".equals( inputType ) ) {
			noEmoji = true;
		} else {
			type = InputType.TYPE_CLASS_TEXT;
			if ( !myIsPassword )
				type |= InputType.TYPE_TEXT_VARIATION_NORMAL;
		}

		if ( myIsPassword )
			type |= InputType.TYPE_TEXT_VARIATION_PASSWORD;

		type |= myCoronaRuntime.getController().getAndroidVersionSpecific().inputTypeFlagsNoSuggestions();
		
		if ( type != 0 )
			setInputType(type);
	}
	
	public String getTextViewInputType()
	{
		String result = "unknown";
		
		KeyListener listener = getKeyListener();
		if ( listener != null )
		{
			int inputType = listener.getInputType();
			switch ( inputType & InputType.TYPE_MASK_CLASS )
			{
			case InputType.TYPE_CLASS_NUMBER:
				result = "number";
				break;
			case InputType.TYPE_CLASS_PHONE:
				result = "phone";
				break;
			case InputType.TYPE_CLASS_TEXT:
				switch ( inputType & InputType.TYPE_MASK_VARIATION ) {
				case InputType.TYPE_TEXT_VARIATION_URI:
					result = "url";
					break;
				case InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS:
					result = "email";
					break;
				default:
					result = "default";
					break;
				}
				break;
			default:
				result = "default";
				break;
			}
		}
			
		return result;
	}

	public void setTextViewPassword( boolean isPassword )
	{
		TransformationMethod method;

		if ( isPassword ) {
			method = PasswordTransformationMethod.getInstance();
			setTextViewInputType("password");
		}
		else
			method = SingleLineTransformationMethod.getInstance();

		setTransformationMethod(method);

		myIsPassword = isPassword;

	}

	public boolean getTextViewPassword()
	{
		return myIsPassword;
	}

	public void setTextViewAlign( String align )
	{
		int gravity = Gravity.LEFT;
		
		if ( "left".equals( align ) )
		{
			gravity = Gravity.LEFT;
		}
		else if ( "center".equals( align ) )
		{
			gravity = Gravity.CENTER_HORIZONTAL;
		}
		else if ( "right".equals( align ) )
		{
			gravity = Gravity.RIGHT;
		}
		gravity |= getGravity() & Gravity.VERTICAL_GRAVITY_MASK;
		setGravity( gravity );
	}
	
	public String getTextViewAlign()
	{
		String result = "";
		
		switch ( getGravity() & Gravity.HORIZONTAL_GRAVITY_MASK )
		{
		case Gravity.LEFT:
			result = "left";
			break;
		case Gravity.RIGHT:
			result = "right";
			break;
		case Gravity.CENTER_HORIZONTAL:
			result = "center";
			break;
		default:
			result = "unknown";
		}

		return result;
	}

	public void setTextViewColor( int color )
	{
		myTextColor = color;
		setTextColor( color );
	}
	
	public int getTextViewColor()
	{
		return myTextColor;
	}

	public void setTextViewText(String text)
	{
		if (text == null) {
			text = "";
		}
		if ((myMaxLength > 0) && (text.length() > myMaxLength)) {
			text = text.substring(0, myMaxLength);
		}

		// Remember the current cursor/selection so it can be restored after the update.
		// Otherwise setText() moves the cursor to position 0, which makes typing and
		// deleting behave erratically whenever the app assigns the ".text" property while
		// the field is being edited (e.g. input filtering/formatting in a userInput listener).
		int selectionStartIndex = getSelectionStart();
		int selectionEndIndex = getSelectionEnd();

		setText(text, TextView.BufferType.EDITABLE);

		// Restore the cursor/selection, clamped to the new text length.
		int textLength = getText().length();
		selectionStartIndex = Math.max(0, Math.min(selectionStartIndex, textLength));
		selectionEndIndex = Math.max(0, Math.min(selectionEndIndex, textLength));
		setSelection(selectionStartIndex, selectionEndIndex);
	}

	public String getTextViewText()
	{
		return getTextString();
	}

	public String getTextViewPlaceholder()
	{
		CharSequence hint = getHint();
		return (hint != null) ? hint.toString() : "";
	}

	public void setTextViewPlaceholder(String placeholder)
	{
		setHint(placeholder);
	}

	public void setTextViewMaxLength(int maxLength)
	{
		if (maxLength < 0) {
			maxLength = 0;
		}

		myMaxLength = maxLength;
		if (myMaxLength > 0) {
			setFilters(new InputFilter[] { new InputFilter.LengthFilter(myMaxLength) });
		}
		else {
			setFilters(new InputFilter[0]);
		}

		String text = getTextString();
		if ((myMaxLength > 0) && (text != null) && (text.length() > myMaxLength)) {
			setTextViewText(text);
		}
	}

	public int getTextViewMaxLength()
	{
		return myMaxLength;
	}

	public void setTextSelection(int startPositionFinal, int endPositionFinal)
	{
		int maxLength = getText().length();
		int startPosition = startPositionFinal;
		int endPosition = endPositionFinal;
		if (startPosition > maxLength) {
			startPosition = maxLength;
			endPosition = maxLength;
		}

		if (endPosition > maxLength) {
			endPosition = maxLength;
		}

		if (startPosition < 0) {
			startPosition = 0;
		}

		if (endPosition < 0) {
			endPosition = 0;
		}

		if (startPosition > endPosition) {
			startPosition = endPosition;
		}

		setSelection(startPosition, endPosition);
	}

	public void setTextReturnKey(String imeType)
	{
		int ime = EditorInfo.IME_ACTION_DONE;
		if ("go".equals(imeType)) {
			ime = EditorInfo.IME_ACTION_GO;
		} else if ("next".equals(imeType)) {
			ime = EditorInfo.IME_ACTION_NEXT;
		} else if ("none".equals(imeType)) {
			ime = EditorInfo.IME_ACTION_NONE;
		} else if ("search".equals(imeType)) {
			ime = EditorInfo.IME_ACTION_SEARCH;
		} else if ("send".equals(imeType)) {
			ime = EditorInfo.IME_ACTION_SEND;
		}
		setImeOptions(ime);
	}
	
	public void setText(CharSequence text, TextView.BufferType type) {
		// Disable the text listener.
		boolean savedEditing = isEditing;
		isEditing = false;

		// Update this field's text.
		super.setText(text, type);

		// Re-enable the text listener.
		isEditing = savedEditing;
	}

	public String getTextString() {
		// If this method was not called on the main UI thread, then fetch a copy of the last updated text.
		// This avoids a possible crash/race-condition since getText() returns a mutable string.
		android.os.Looper mainLooper = android.os.Looper.getMainLooper();
		if ((mainLooper == null) || (mainLooper.getThread() != Thread.currentThread())) {
			return myCurrentText;
		}

		// We're on the main UI thread. Return this field's current string.
		return getText().toString();
	}
	
	public void setReadOnly(boolean value) {
		setKeyListener(value ? null : myKeyListener);
		setScroller(new Scroller(getContext()));
		setVerticalScrollBarEnabled(value);
		setMovementMethod(value ? new ScrollingMovementMethod(): getDefaultMovementMethod());
		setTransformationMethod(new HideReturnsTransformationMethod());
	}
	
	public boolean isReadOnly() {
		return (getKeyListener() == null);
	}

	public int getBorderPaddingTop() {
		fetchBorderPadding();
		return sBorderPaddingTop;
	}

	public int getBorderPaddingBottom() {
		fetchBorderPadding();
		return sBorderPaddingBottom;
	}

	public int getBorderPaddingLeft() {
		fetchBorderPadding();
		return sBorderPaddingLeft;
	}

	public int getBorderPaddingRight() {
		fetchBorderPadding();
		return sBorderPaddingRight;
	}

	private void fetchBorderPadding() {
		// Do not continue if we have already fetched the border padding.
		if ((sBorderPaddingTop >= 0) && (sBorderPaddingBottom >= 0) &&
		    (sBorderPaddingLeft >= 0) && (sBorderPaddingRight >= 0)) {
			return;
		}

		// Initialize the static padding variables. Assume no padding.
		sBorderPaddingTop = 0;
		sBorderPaddingBottom = 0;
		sBorderPaddingLeft = 0;
		sBorderPaddingRight = 0;

		// Fetch this view's width and height in pixels.
		int width = getWidth();
		int height = getHeight();
		if ((width <= 0) || (height <= 0)) {
			// A width and height has not been assigned to this view yet.
			// Force it to be the minimum size supported.
			measure(android.view.View.MeasureSpec.UNSPECIFIED, android.view.View.MeasureSpec.UNSPECIFIED);
			layout(0, 0, getMeasuredWidth(), getMeasuredHeight());
			width = getWidth();
			height = getHeight();
		}

		// Draw this view to a bitmap.
		android.graphics.Bitmap bitmap = null;
		try {
			bitmap = android.graphics.Bitmap.createBitmap(width, height, android.graphics.Bitmap.Config.ARGB_8888);
			draw(new android.graphics.Canvas(bitmap));
		}
		catch (Exception ex) { }
		if (bitmap == null) {
			return;
		}

		// Scan through the image's pixels, finding where its border begins and ends.
		boolean hasFoundTopBorder = false;
		boolean hasFoundBottomBorder = false;
		boolean hasFoundLeftBorder = false;
		boolean hasFoundRightBorder = false;
		int topBorderIndex = -1;
		int bottomBorderIndex = -1;
		int leftBorderIndex = -1;
		int rightBorderIndex = -1;
		for (int yIndex = 0; yIndex < height; yIndex++) {
			boolean rowHasOpaquePixel = false;
			int leftOpaquePixelIndex = -1;
			int rightOpaquePixelIndex = -1;
			for (int xIndex = 0; xIndex < width; xIndex++) {
				int alpha = android.graphics.Color.alpha(bitmap.getPixel(xIndex, yIndex));
				if (alpha > 64) {
					if (rowHasOpaquePixel == false) {
						leftOpaquePixelIndex = xIndex;
					}
					rightOpaquePixelIndex = xIndex;
					rowHasOpaquePixel = true;
				}
			}
			if (rowHasOpaquePixel) {
				if (hasFoundTopBorder == false) {
					topBorderIndex = yIndex;
					hasFoundTopBorder = true;
				}
				bottomBorderIndex = yIndex;
				hasFoundBottomBorder = true;

				if (hasFoundLeftBorder == false) {
					leftBorderIndex = leftOpaquePixelIndex;
					hasFoundLeftBorder = true;
				}
				else if (leftOpaquePixelIndex < leftBorderIndex) {
					leftBorderIndex = leftOpaquePixelIndex;
				}

				if (hasFoundRightBorder == false) {
					rightBorderIndex = rightOpaquePixelIndex;
					hasFoundRightBorder = true;
				}
				else if (rightOpaquePixelIndex < rightBorderIndex) {
					rightBorderIndex = rightOpaquePixelIndex;
				}
			}
		}

		// Calculate the padding size between the edges of the view and it painted border.
		if (hasFoundTopBorder) {
			sBorderPaddingTop = topBorderIndex;
		}
		if (hasFoundBottomBorder) {
			sBorderPaddingBottom = height - (bottomBorderIndex + 1);
		}
		if (hasFoundLeftBorder) {
			sBorderPaddingLeft = leftBorderIndex;
		}
		if (hasFoundRightBorder) {
			sBorderPaddingRight = width - (rightBorderIndex + 1);
		}

		// Release the bitmap.
		// bitmap.recycle();
	}
}
