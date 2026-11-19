const initialized = new WeakSet();

const initializeChecklist = (checklist) => {
  const status = checklist.querySelector("[data-checklist-status]");
  const clear = checklist.querySelector("[data-checklist-clear]");
  if (!status || !clear || initialized.has(checklist)) {
    return;
  }
  initialized.add(checklist);

  const storagePrefix = `z80romlesssbc:${checklist.dataset.checklist}:v1:`;
  const boxes = [...checklist.querySelectorAll("input[data-checklist-id]")];
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
    if (!globalThis.confirm(`Clear all ${checklist.dataset.checklistLabel} ticks in this browser?`)) {
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

const initializeChecklists = () => {
  document.querySelectorAll("[data-checklist]").forEach(initializeChecklist);
};

initializeChecklists();

if (globalThis.document$) {
  globalThis.document$.subscribe(initializeChecklists);
}
