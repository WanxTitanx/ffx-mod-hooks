"use strict";
const $ = (s) => document.querySelector(s);
let model,
  selected = null,
  tab = "refine",
  confirmation = null;
const text = (tag, value, cls) => {
  const n = document.createElement(tag);
  n.textContent = value;
  if (cls) n.className = cls;
  return n;
};
const notice = (value) => {
  $("#notice").textContent = value;
};
async function api(path, data) {
  const response = await fetch("/api/" + path, {
    method: data ? "POST" : "GET",
    headers: data
      ? { "Content-Type": "application/json", "X-Workshop-Token": model.token }
      : {},
    body: data ? JSON.stringify(data) : undefined,
  });
  const result = await response.json();
  if (!response.ok) throw Error(result.error || "Unable to apply change");
  return result;
}
function current() {
  return model.pieces.find((p) => p.slot === selected);
}
function button(label, action, cls = "primary") {
  const b = text("button", label, cls);
  b.type = "button";
  b.addEventListener("click", action);
  return b;
}
function select(label, options, id) {
  const wrapper = text("label", "", "field");
  wrapper.append(text("span", label));
  const s = document.createElement("select");
  s.id = id;
  s.setAttribute("aria-label", label);
  options.forEach((o) => {
    const opt = text("option", o.name);
    opt.value = o.id;
    s.append(opt);
  });
  wrapper.append(s);
  return wrapper;
}
function inventory() {
  const query = $("#search").value.toLowerCase(),
    owner = $("#owner").value;
  const list = $("#inventory");
  list.replaceChildren();
  const pieces = model.pieces.filter(
    (p) =>
      (!owner || p.owner === owner) &&
      `${p.owner} ${p.kind} ${p.abilities.map((a) => a.name).join(" ")}`
        .toLowerCase()
        .includes(query),
  );
  $("#count").textContent = pieces.length;
  for (const p of pieces) {
    const b = button(
      "",
      () => {
        selected = p.slot;
        render();
      },
      "inventory-item" + (p.slot === selected ? " active" : ""),
    );
    b.setAttribute(
      "aria-label",
      `${p.owner} ${p.kind}, piece ${p.id}${p.equipped ? ", equipped" : ""}`,
    );
    b.setAttribute("aria-pressed", String(p.slot === selected));
    b.append(
      text("span", p.mode === 2 ? "+" + p.total : "+" + p.rank, "badge"),
    );
    b.append(text("strong", p.owner + " · " + p.kind));
    b.append(
      text(
        "small",
        `Piece ${p.id} · ${p.equipped ? "Equipped" : p.protected ? "Special gear" : p.capacity + " slots"}${p.fifth ? " + extension" : ""}`,
      ),
    );
    const item = document.createElement("div");
    item.setAttribute("role", "listitem");
    item.append(b);
    list.append(item);
  }
  if (!pieces.length)
    list.append(text("p", "No matching pieces.", "description"));
}
function render() {
  inventory();
  $("#session").textContent =
    `Workspace ${model.identity} · saved revision ${model.revision}`;
  const p = current();
  $("#empty").hidden = !!p;
  $("#selected").hidden = !p;
  if (!p) return;
  $("#piece-owner").textContent =
    p.owner.toUpperCase() + " / " + p.kind.toUpperCase();
  $("#piece-title").textContent = p.kind + " · Piece " + p.id;
  $("#piece-sub").textContent = p.equipped
    ? "Currently equipped — unequip before changing this piece."
    : p.protected
      ? "Special equipment — fusion receiver only."
      : "Ready for your next change.";
  $("#rank").textContent = "+" + (p.mode === 2 ? p.total : p.rank);
  $("#rank-label").textContent =
    p.mode === 2
      ? `of ${p.maximum} · per ability`
      : p.mode === 1
        ? "of 10 · whole piece"
        : "Choose a refinement path";
  const cards = $("#abilities");
  cards.replaceChildren();
  for (let i = 0; i < 5; i++) {
    const a = p.abilities[i],
      locked = !a || !a.available;
    const card = text(
      "div",
      "",
      `ability${i === 4 ? " fifth" : ""}${locked ? " locked" : ""}`,
    );
    card.append(text("small", i === 4 ? "FIFTH ABILITY" : "SLOT " + (i + 1)));
    card.append(text("strong", locked ? "Not unlocked" : a.name));
    if (!locked && a.id !== 255) card.append(text("em", "+" + a.rank));
    cards.append(card);
  }
  $("#piece-note").textContent = p.abilities.some(
    (a) => !a.supported && a.id !== 255,
  )
    ? "Refinement is unavailable for this combination. Some abilities still need their own effect integration."
    : "Numeric abilities gain one percentage point per rank. Refined Auto-Protect/Shell add a bounded reduction after the native status.";
  operation();
}
async function preview(op, extra = {}) {
  try {
    notice("");
    const p = current();
    const result = await api("preview", {
      op,
      slot: p.slot,
      piece: p.id,
      revision: model.revision,
      ...extra,
    });
    confirmation = result.confirmation;
    $("#confirm-title").textContent = result.random
      ? "Refine one eligible ability?"
      : "Confirm " + op.replaceAll("_", " ") + "?";
    $("#confirm-summary").textContent = result.random
      ? "One occupied ability below +10 will gain a rank. The material cost is the same for every possible result."
      : `${p.owner} · ${p.kind} · Piece ${p.id}`;
    const costs = $("#costs");
    costs.replaceChildren();
    for (const c of result.costs) {
      const row = text("div", "", "cost-row");
      row.append(
        text("span", c.name),
        text("strong", `−${c.amount} / ${c.have} owned`),
      );
      costs.append(row);
    }
    if (!result.costs.length) costs.append(text("p", "No materials required."));
    if (result.after) {
      const a = result.after,
        b = result.before,
        changes = [];
      if (a.owner !== b.owner || a.kind !== b.kind)
        changes.push(`${b.owner} ${b.kind} → ${a.owner} ${a.kind}`);
      if (a.capacity !== b.capacity)
        changes.push(`${b.capacity} → ${a.capacity} original slots`);
      if (a.mode !== b.mode)
        changes.push(
          a.mode === 1
            ? "Path A · whole-piece refinement"
            : "Path B · one ability per attempt",
        );
      if (a.rank !== b.rank) changes.push(`Piece rank +${b.rank} → +${a.rank}`);
      for (const next of a.abilities) {
        const old = b.abilities.find((x) => x.slot === next.slot);
        if (!old || old.id !== next.id || old.rank !== next.rank)
          changes.push(
            `Slot ${next.slot + 1}: ${old?.name ?? "Locked"} → ${next.name} +${next.rank}`,
          );
      }
      for (const change of changes) costs.prepend(text("p", change));
    }
    if (op === "fuse")
      $("#confirm-summary").textContent +=
        " — the donor piece will be consumed.";
    $("#confirm").returnValue = "";
    $("#confirm").showModal();
  } catch (e) {
    notice(e.message);
  }
}
function operation() {
  const p = current(),
    body = $("#operation-body");
  body.replaceChildren();
  document.querySelectorAll("[data-op]").forEach((b) => {
    b.setAttribute("aria-selected", String(b.dataset.op === tab));
    b.tabIndex = b.dataset.op === tab ? 0 : -1;
  });
  if (tab === "refine") {
    if (!p.mode) {
      body.append(
        text(
          "p",
          "Choose one path for this piece. This choice stays with the piece, even when it moves in your inventory.",
          "description",
        ),
      );
      const grid = text("div", "", "mode-grid");
      for (const [value, title, copy] of [
        [
          1,
          "A · Refine the whole piece",
          "One rank improves every occupied ability. Each contributes to the material cost. Maximum +10.",
        ],
        [
          2,
          "B · Refine one ability",
          "Each attempt improves one eligible ability. Owner catalyst + Ability Sphere. Up to +50 with five abilities.",
        ],
      ]) {
        const b = button("", () => preview("mode", { value }), "mode-card");
        b.append(text("strong", title), text("small", copy));
        grid.append(b);
      }
      body.append(grid);
    } else {
      body.append(
        text(
          "p",
          p.mode === 1
            ? "Every occupied ability will gain one rank. Adding an ability later includes its catch-up material cost."
            : "One occupied ability below +10 gains a rank. Fully refined abilities leave the pool; a cancelled attempt changes nothing.",
          "description",
        ),
      );
      body.append(button("Preview refinement", () => preview("refine")));
    }
  } else if (tab === "fifth") {
    body.append(
      text(
        "p",
        "The fifth ability belongs to this piece. Its identity and rank stay separate from the four original slots.",
        "description",
      ),
    );
    if (!p.fifth) {
      body.append(
        text(
          "p",
          "Requires four original slots · Lv.4 Key Sphere ×1",
          "inline-note",
        ),
      );
      body.append(
        button("Preview fifth-slot unlock", () => preview("unlock_fifth")),
      );
    } else {
      body.append(
        select("Choose an ability", model.fifth_choices, "fifth-choice"),
      );
      body.append(
        button("Preview ability", () =>
          preview("set_fifth", { value: Number($("#fifth-choice").value) }),
        ),
      );
    }
  } else if (tab === "reshape") {
    const grid = text("div", "", "form-grid");
    const left = document.createElement("div"),
      right = document.createElement("div");
    left.append(
      select("Reforge with an imported model", model.templates, "template"),
    );
    left.append(
      button(
        "Preview reforge",
        () => preview("reforge", { template: $("#template").value }),
        "secondary",
      ),
    );
    const capacities = [1, 2, 3, 4]
      .filter((n) => n > p.capacity)
      .map((n) => ({ id: n, name: n + " original slots" }));
    if (capacities.length) {
      right.append(select("Expand capacity", capacities, "capacity"));
      right.append(
        select(
          "Key Sphere recipe",
          [
            { id: 0, name: "One sphere for each new slot" },
            { id: 1, name: "Quantity matches the new slot number" },
          ],
          "slot-policy",
        ),
      );
      right.append(
        button(
          "Preview expansion",
          () =>
            preview("expand", {
              value: Number($("#capacity").value),
              policy: Number($("#slot-policy").value),
            }),
          "secondary",
        ),
      );
    } else
      right.append(text("p", "Four original slots unlocked.", "description"));
    grid.append(left, right);
    body.append(grid, text("hr", "", "divider"));
    const occupied = p.abilities.filter((a) => a.id !== 255 && a.available);
    if (occupied.length) {
      body.append(
        select(
          "Choose an existing ability",
          occupied.map((a) => ({
            id: a.slot,
            name: `Slot ${a.slot + 1} · ${a.name}`,
          })),
          "clear-slot",
        ),
      );
      const actions = text("div", "", "actions");
      actions.append(
        button(
          "Preview removal",
          () => preview("clear", { value: Number($("#clear-slot").value) }),
          "secondary",
        ),
      );
      actions.append(
        button(
          "Preview evolution",
          () => {
            const a = p.abilities[Number($("#clear-slot").value)],
              id = a.id - 0x8000;
            const next =
              id >= 98 && id <= 120 && (id - 98) % 4 < 3
                ? id + 1
                : id >= 47 && id <= 75 && (id - 47) % 4 === 0
                  ? id - 1
                  : null;
            if (next === null) {
              notice("No verified evolution for this ability.");
              return;
            }
            preview("evolve", { ability_slot: a.slot, value: 0x8000 + next });
          },
          "secondary",
        ),
      );
      body.append(actions);
    }
  } else {
    body.append(
      text(
        "p",
        "Choose a donor and transfer up to two ability instances into this piece. The donor is consumed. Both pieces must use the same refinement path.",
        "description",
      ),
    );
    const donors = model.pieces.filter(
      (x) =>
        x.slot !== p.slot && !x.equipped && !x.protected && x.mode === p.mode,
    );
    if (!donors.length) {
      body.append(
        text(
          "p",
          "No eligible donor with the same refinement path.",
          "inline-note",
        ),
      );
      return;
    }
    body.append(
      select(
        "Donor piece",
        donors.map((x) => ({
          id: x.slot,
          name: `${x.owner} · ${x.kind} · Piece ${x.id}`,
        })),
        "donor",
      ),
    );
    const options = text("div", "", "form-grid");
    body.append(options);
    function transfers() {
      const d = donors.find((x) => x.slot === Number($("#donor").value));
      options.replaceChildren();
      for (let i = 0; i < 2; i++) {
        const pair = document.createElement("div");
        pair.append(
          select(
            "Transfer " + (i + 1),
            [
              { id: -1, name: "Do not transfer" },
              ...d.abilities
                .filter((a) => a.id !== 255 && a.available)
                .map((a) => ({ id: a.slot, name: `${a.name} +${a.rank}` })),
            ],
            "source" + i,
          ),
        );
        pair.append(
          select(
            "Destination",
            p.abilities
              .filter((a) => a.available)
              .map((a) => ({
                id: a.slot,
                name: `Slot ${a.slot + 1} · ${a.name}`,
              })),
            "target" + i,
          ),
        );
        options.append(pair);
      }
    }
    $("#donor").addEventListener("change", transfers);
    transfers();
    body.append(
      button("Preview fusion", () => {
        const selected = [0, 1]
          .filter((i) => Number($("#source" + i).value) >= 0)
          .map((i) => ({
            from: Number($("#source" + i).value),
            to: Number($("#target" + i).value),
          }));
        preview("fuse", {
          other: Number($("#donor").value),
          transfers: selected,
        });
      }),
    );
  }
  body.querySelectorAll("button").forEach((b) => {
    if (p.equipped) b.disabled = true;
  });
}
$("#commit").addEventListener("click", async () => {
  const b = $("#commit");
  b.disabled = true;
  try {
    model = await api("confirm", { confirmation });
    $("#confirm").close("committed");
    notice("Saved. Your piece and material balance have been verified.");
    render();
  } catch (e) {
    $("#confirm").close();
    notice(e.message);
  } finally {
    b.disabled = false;
  }
});
$("#confirm").addEventListener("close", () => {
  if ($("#confirm").returnValue !== "committed")
    api("cancel", {}).catch(() => {});
  confirmation = null;
});
$("#search").addEventListener("input", inventory);
$("#owner").addEventListener("change", inventory);
document.querySelectorAll("[data-op]").forEach((b) =>
  b.addEventListener("click", () => {
    tab = b.dataset.op;
    operation();
  }),
);
$("#inventory").addEventListener("keydown", (e) => {
  if (!["ArrowUp", "ArrowDown"].includes(e.key)) return;
  const buttons = [...$("#inventory").querySelectorAll("button")],
    at = buttons.indexOf(document.activeElement);
  if (at < 0) return;
  e.preventDefault();
  buttons[
    Math.min(
      buttons.length - 1,
      Math.max(0, at + (e.key === "ArrowDown" ? 1 : -1)),
    )
  ].focus();
});
api("state")
  .then((data) => {
    model = data;
    for (const owner of [...new Set(model.pieces.map((p) => p.owner))]) {
      const o = text("option", owner);
      o.value = owner;
      $("#owner").append(o);
    }
    selected =
      model.pieces.find((p) => !p.equipped && !p.protected)?.slot ??
      model.pieces[0]?.slot ??
      null;
    render();
  })
  .catch((e) => notice(e.message));

$(".op-tabs").addEventListener("keydown", (event) => {
  if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) return;
  const tabs = [...document.querySelectorAll("[data-op]")],
    at = tabs.indexOf(document.activeElement);
  if (at < 0) return;
  event.preventDefault();
  const next =
    event.key === "Home"
      ? 0
      : event.key === "End"
        ? tabs.length - 1
        : (at + (event.key === "ArrowRight" ? 1 : -1) + tabs.length) %
          tabs.length;
  tab = tabs[next].dataset.op;
  operation();
  tabs[next].focus();
});
