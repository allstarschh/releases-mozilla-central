/* vim:set ts=2 sw=2 sts=2 et: */
/* ***** BEGIN LICENSE BLOCK *****
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 * ***** END LICENSE BLOCK ***** */

const TEST_URI = "http://example.com/browser/browser/devtools/webconsole/test" +
                 "/test-bug-782653-css-errors.html";

let nodes;

let styleEditorWin;

function test() {
  addTab(TEST_URI);
  browser.addEventListener("load", function onLoad() {
    browser.removeEventListener("load", onLoad, true);
    openConsole(null, testViewSource);
  }, true);
}

function testViewSource(hud) {

  waitForSuccess({
    name: "find the location node",
    validatorFn: function()
    {
      return hud.outputNode.querySelector(".webconsole-location");
    },
    successFn: function()
    {
      nodes = hud.outputNode.querySelectorAll(".webconsole-location");

      Services.ww.registerNotification(observer);

      EventUtils.sendMouseEvent({ type: "click" }, nodes[0]);
    },
    failureFn: finishTest,
  });
}

function checkStyleEditorForSheetAndLine(aStyleSheetIndex, aLine, aCallback) {

  function doCheck(aEditor) {
    function checkLineAndCallback() {
      info("In checkLineAndCallback()");
      ok(aEditor.sourceEditor != null, "sourceeditor not null");
      ok(aEditor.sourceEditor.getCaretPosition() != null, "position not null");
      ok(aEditor.sourceEditor.getCaretPosition().line != null, "line not null");
      is(aEditor.sourceEditor.getCaretPosition().line, aLine,
         "Correct line is selected");
      if (aCallback) {
        aCallback();
      }
    }

    ok(aEditor, "aEditor is defined.");

    // Source-editor is already loaded, check the current line of caret.
    if (aEditor.sourceEditor) {
      if (aEditor.styleSheetIndex != SEC.selectedStyleSheetIndex) {
        ok(false, "Correct Style Sheet was not selected.");
        if (aCallback) {
          executeSoon(aCallback);
        }
        return;
      }

      info("Correct Style Sheet is selected in the editor");
      info("Editor is already loaded, check the current line of caret");
      executeSoon(checkLineAndCallback);
      return;
    }

    info("source editor is not loaded, waiting for it.");
    // Wait for source editor to be loaded.
    aEditor.addActionListener({
      onAttach: function onAttach() {
        info("on attach happened");
        aEditor.removeActionListener(this);
        info("this removed");
        executeSoon(function() {
          if (aEditor.styleSheetIndex != SEC.selectedStyleSheetIndex) {
            ok(false, "Correct Style Sheet was not selected.");
            if (aCallback) {
              aCallback();
            }
            return;
          }
          checkLineAndCallback()
        });
      }
    });
  }

  let SEC = styleEditorWin.styleEditorChrome;
  ok(SEC, "Syle Editor Chrome is defined properly while calling for [" +
          aStyleSheetIndex + ", " + aLine + "]");

  // Editors are not ready, so wait for them.
  if (!SEC.editors.length) {
    info("Editor is not ready, waiting before doing check.");
    SEC.addChromeListener({
      onEditorAdded: function onEditorAdded(aChrome, aEditor) {
        info("Editor loaded now. Removing listener and doing check.");
        aChrome.removeChromeListener(this);
        executeSoon(function() {
          doCheck(aEditor);
        });
      }
    });
  }
  // Execute soon so that selectedStyleSheetIndex has correct value.
  else {
    info("Editor is defined, opening the desired editor for now and " +
         "checking later if it is correct");
    for (let aEditor of SEC.editors) {
      if (aEditor.styleSheetIndex == aStyleSheetIndex) {
        doCheck(aEditor);
        break;
      }
    }
  }
}

let observer = {
  observe: function(aSubject, aTopic, aData) {
    if (aTopic != "domwindowopened") {
      return;
    }
    Services.ww.unregisterNotification(observer);
    info("Style Editor window was opened in response to clicking " +
         "the location node");

    executeSoon(function() {
      styleEditorWin = window.StyleEditor
                             .StyleEditorManager
                             .getEditorForWindow(content.window);
      ok(styleEditorWin, "Style Editor Window is defined");
      waitForFocus(function() {
        //styleEditorWin.addEventListener("load", function onStyleEditorWinLoad() {
          //styleEditorWin.removeEventListener("load", onStyleEditorWinLoad);

          checkStyleEditorForSheetAndLine(0, 7, function() {
            checkStyleEditorForSheetAndLine(1, 6, function() {
              window.StyleEditor.toggle();
              styleEditorWin = null;
              finishTest();
            });
            EventUtils.sendMouseEvent({ type: "click" }, nodes[1]);
          });
        //});
      }, styleEditorWin);
    });
  }
};
