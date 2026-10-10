const storagePrefix = "z80romlesssbc:phase-0-passives:v1:";
const initialized = new WeakSet();

const initializeChecklist = () => {
  const status = document.getElementById("phase-0-checklist-status");
  const clear = document.getElementById("clear-phase-0-checklist");
  if (!status || !clear || initialized.has(status)) {
    return;
  }
  initialized.add(status);

  const boxes = [...document.querySelectorAll("input[data-checklist-id]")];
  const keyFor = (box) => storagePrefix + box.dataset.checklistId;
  let storageFailed = false;
  const reportError = (message, error) => {
    storageFailed = true;
    status.textContent = message;
    console.warn(message, error);
  };

  try {
    for (const box of boxes) {
      box.checked = localStorage.getItem(keyFor(box)) === "1";
    }
    status.textContent = "Progress is saved in this browser only.";
  } catch (error) {
    reportError(
      "Saved progress could not be restored. Checkboxes still work, but browser storage may be unavailable.",
      error,
    );
  }

  for (const box of boxes) {
    box.addEventListener("change", () => {
      try {
        if (box.checked) {
          localStorage.setItem(keyFor(box), "1");
        } else {
          localStorage.removeItem(keyFor(box));
        }
        if (!storageFailed) {
          status.textContent = "Progress is saved in this browser only.";
        }
      } catch (error) {
        reportError(
          "This change could not be saved. Checkboxes still work, but progress may be lost when you leave this page.",
          error,
        );
      }
    });
  }

  clear.hidden = false;
  clear.addEventListener("click", () => {
    if (!globalThis.confirm("Clear all Phase 0 installation ticks in this browser?")) {
      return;
    }
    for (const box of boxes) {
      box.checked = false;
    }
    try {
      for (const box of boxes) {
        localStorage.removeItem(keyFor(box));
      }
      storageFailed = false;
      status.textContent = "Checklist cleared in this browser.";
    } catch (error) {
      reportError(
        "Saved progress could not be fully cleared. Some ticks may return when you reload this page.",
        error,
      );
    }
  });
};

initializeChecklist();

if (globalThis.document$) {
  globalThis.document$.subscribe(initializeChecklist);
}
