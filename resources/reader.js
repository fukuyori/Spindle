// Injected into every chapter page. Renders highlights and reports new
// selections back to C++ via the QWebChannel `spindle` object.
//
// Position model (shared with C++, see src/core/BlockIndex.* and Highlight.*):
//   - Each leaf block element carries data-spindle-block="N" (injected by C++).
//     JS reads these numbers; it never computes block order itself.
//   - A highlight is { block, side, offset, length }. `offset`/`length` are
//     character counts (UTF-16 units) into the text of the chosen *side*:
//       side "original"    -> the block element's own text
//       side "translation" -> the block's .spindle-translation sibling text
//     counting forward across following blocks (block-only, inter-block text
//     excluded), bounded by the chapter; never crossing sides or chapters.
//   - A highlight is shown only on the side it was made on (character-precise);
//     it is never mirrored onto the other side (no cross-side display).
(function () {
  if (window.__spindleReady) return;
  window.__spindleReady = true;

  var COLORS = {
    yellow: "rgba(255,215,64,0.45)",
    blue: "rgba(64,196,255,0.40)",
    pink: "rgba(255,128,171,0.40)",
    orange: "rgba(255,167,38,0.45)",
    green: "rgba(105,240,174,0.40)",
    purple: "rgba(179,136,255,0.40)"
  };

  var BLOCK_SELECTOR = "p, h1, h2, h3, h4, h5, h6, li, blockquote, figcaption, dd, dt";
  var currentLang = "";
  var reapplyTimer = null;

  // --- blocks ------------------------------------------------------------
  function leafBlocks() {
    // Injected by C++, returned in document order.
    return Array.prototype.slice.call(document.querySelectorAll("[data-spindle-block]"));
  }
  function blockByNumber(n) {
    return document.querySelector('[data-spindle-block="' + n + '"]');
  }
  // The text container for a block on a given side.
  function sideContainer(blockEl, side) {
    if (side === "translation") {
      var t = blockEl.nextElementSibling;
      return t && t.classList.contains("spindle-translation") ? t : null;
    }
    return blockEl;
  }
  // Text nodes inside a container, excluding highlight marks and (for the
  // original side) any inserted translation text.
  function containerTextNodes(container, side) {
    var out = [];
    var walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT, {
      acceptNode: function (node) {
        var p = node.parentElement;
        if (p && p.closest("mark.spindle-hl")) return NodeFilter.FILTER_REJECT;
        if (side !== "translation" && p && p.closest(".spindle-translation"))
          return NodeFilter.FILTER_REJECT;
        return NodeFilter.FILTER_ACCEPT;
      }
    });
    var n;
    while ((n = walker.nextNode())) out.push(n);
    return out;
  }
  // Ordered {node,start,end,block} list across the side's blocks, starting at the
  // anchor block, cumulative offset 0 = first char of the anchor block's side
  // text. Stops once `need` chars are covered (Infinity = whole chapter).
  function sideOffsetList(anchorBlockEl, side, need) {
    var blocks = leafBlocks();
    var startIdx = blocks.indexOf(anchorBlockEl);
    if (startIdx < 0) return [];
    var items = [], offset = 0;
    for (var bi = startIdx; bi < blocks.length; bi++) {
      var container = sideContainer(blocks[bi], side);
      if (!container) {
        if (side === "translation") break; // untranslated gap: stop
        continue;
      }
      var nodes = containerTextNodes(container, side);
      for (var k = 0; k < nodes.length; k++) {
        var len = nodes[k].length;
        items.push({ node: nodes[k], start: offset, end: offset + len, block: blocks[bi] });
        offset += len;
      }
      if (offset >= need) break;
    }
    return items;
  }

  // --- rendering ---------------------------------------------------------
  function wrapPortion(node, ls, le, h) {
    var text = node.nodeValue || "";
    if (ls < 0 || le > text.length || ls >= le) return;
    var parent = node.parentNode;
    if (!parent) return;
    var before = text.slice(0, ls), middle = text.slice(ls, le), after = text.slice(le);
    var mark = document.createElement("mark");
    mark.className = "spindle-hl";
    mark.setAttribute("data-hl-id", h.id);
    mark.style.backgroundColor = COLORS[h.color] || COLORS.yellow;
    mark.style.color = "inherit";
    mark.style.borderRadius = "2px";
    if (h.note) mark.title = h.note;
    mark.textContent = middle;
    var frag = document.createDocumentFragment();
    if (before) frag.appendChild(document.createTextNode(before));
    frag.appendChild(mark);
    if (after) frag.appendChild(document.createTextNode(after));
    parent.replaceChild(frag, node);
  }

  // A highlight is shown only on the side it was made on (original or
  // translation), character-precise. There is no cross-side display: original
  // highlights are not shown on the translation, and vice versa.
  function applyOne(h) {
    var anchor = blockByNumber(h.block);
    if (!anchor) return;
    var side = h.side === "translation" ? "translation" : "original";
    // A translation-side highlight only renders when the shown translation is
    // the same language it was made in.
    if (side === "translation" && h.lang && currentLang && h.lang !== currentLang)
      return;

    var s = h.offset, e = h.offset + h.length;
    var list = sideOffsetList(anchor, side, e);
    var si = -1, ei = -1;
    for (var i = 0; i < list.length; i++) {
      if (si < 0 && s >= list[i].start && s < list[i].end) si = i;
      if (e > list[i].start && e <= list[i].end) ei = i;
    }
    if (si < 0 || ei < 0) return;
    for (var j = ei; j >= si; j--) {
      var it = list[j];
      var ls = j === si ? s - it.start : 0;
      var le = j === ei ? e - it.start : it.end - it.start;
      if (ls >= le) continue;
      wrapPortion(it.node, ls, le, h);
    }
  }

  function clearMarks() {
    var marks = document.querySelectorAll("mark.spindle-hl");
    for (var i = 0; i < marks.length; i++) {
      var m = marks[i];
      var parent = m.parentNode;
      if (!parent) continue;
      while (m.firstChild) parent.insertBefore(m.firstChild, m);
      parent.removeChild(m);
      if (parent.normalize) parent.normalize();
    }
  }

  function attachClickHandlers() {
    var nodes = document.querySelectorAll("mark.spindle-hl");
    for (var k = 0; k < nodes.length; k++) {
      nodes[k].addEventListener("click", function (e) {
        e.preventDefault();
        e.stopPropagation();
        var id = this.getAttribute("data-hl-id");
        if (window.spindle && id) window.spindle.markClicked(id);
      });
    }
  }

  function applyAll() {
    if (!window.spindle) return;
    clearMarks();
    window.spindle.currentHighlights(function (jsonStr) {
      var arr;
      try { arr = JSON.parse(jsonStr); } catch (e) { return; }
      window.__spindleHighlights = arr || []; // for list jumps (see onScrollHighlight)
      if (!arr || !arr.length) return;
      // Apply later offsets first so earlier ones stay valid as marks (excluded
      // from the text walk) are inserted.
      arr.sort(function (a, b) {
        if (a.block !== b.block) return b.block - a.block;
        return b.offset - a.offset;
      });
      arr.forEach(applyOne);
      attachClickHandlers();
    });
  }

  function scheduleReapply() {
    if (reapplyTimer) clearTimeout(reapplyTimer);
    reapplyTimer = setTimeout(function () { reapplyTimer = null; applyAll(); }, 120);
  }

  // --- selection / creation ---------------------------------------------
  function isNodeAfter(node, ref) {
    return (ref.compareDocumentPosition(node) & Node.DOCUMENT_POSITION_FOLLOWING) !== 0;
  }
  function isNodeBefore(node, ref) {
    return (ref.compareDocumentPosition(node) & Node.DOCUMENT_POSITION_PRECEDING) !== 0;
  }
  function elementOf(node) {
    return node && node.nodeType === Node.ELEMENT_NODE ? node : (node ? node.parentElement : null);
  }
  function sideOfNode(node) {
    var el = elementOf(node);
    if (!el) return null;
    if (el.closest(".spindle-translation")) return "translation";
    if (el.closest("[data-spindle-block]")) return "original";
    return null;
  }
  function anchorBlockFor(node, side) {
    var el = elementOf(node);
    if (!el) return null;
    if (side === "translation") {
      var t = el.closest(".spindle-translation");
      if (!t) return null;
      var b = t.previousElementSibling;
      return b && b.hasAttribute("data-spindle-block") ? b : null;
    }
    return el.closest("[data-spindle-block]");
  }
  // Map a (container, offset) boundary to a cumulative offset within `items`.
  function boundaryToOffset(items, container, offset, isStart) {
    if (container.nodeType === Node.TEXT_NODE) {
      for (var i = 0; i < items.length; i++)
        if (items[i].node === container) return items[i].start + offset;
      return null;
    }
    var boundaryNode = container.childNodes[offset] || null;
    if (isStart) {
      for (var a = 0; a < items.length; a++) {
        var it = items[a];
        if (boundaryNode === null || boundaryNode.contains(it.node) || isNodeAfter(it.node, boundaryNode))
          return it.start;
      }
      return null;
    }
    for (var b = items.length - 1; b >= 0; b--) {
      var jt = items[b];
      if (boundaryNode === null || isNodeBefore(jt.node, boundaryNode)) return jt.end;
    }
    return null;
  }

  function onMouseUp() {
    var sel = window.getSelection();
    if (!sel || sel.isCollapsed || sel.rangeCount === 0) return;
    var range = sel.getRangeAt(0);
    var text = sel.toString();
    if (!text.trim()) return;
    if (!document.body.contains(range.startContainer) || !document.body.contains(range.endContainer)) return;

    var side = sideOfNode(range.startContainer);
    if (!side) return;
    // A single highlight cannot span original and translation.
    if (sideOfNode(range.endContainer) !== side) return;

    var anchor = anchorBlockFor(range.startContainer, side);
    if (!anchor) return;
    var block = parseInt(anchor.getAttribute("data-spindle-block"), 10);
    if (isNaN(block)) return;

    var items = sideOffsetList(anchor, side, Infinity);
    var start = boundaryToOffset(items, range.startContainer, range.startOffset, true);
    var end = boundaryToOffset(items, range.endContainer, range.endOffset, false);
    if (start === null || end === null || start >= end) return;

    if (window.spindle)
      window.spindle.selectionMade(block, side, currentLang || "", start, end - start, text);
  }

  // --- translation -------------------------------------------------------
  function collectLeafBlocks() {
    var nodes = document.body.querySelectorAll(BLOCK_SELECTOR);
    var out = [];
    for (var i = 0; i < nodes.length; i++) {
      var el = nodes[i];
      if (el.classList.contains("spindle-translation")) continue;
      if (el.querySelector(BLOCK_SELECTOR)) continue; // not a leaf
      var text = (el.textContent || "").trim();
      if (text.length < 2) continue;
      if (!/[\p{L}\p{N}]/u.test(text)) continue;
      out.push(el);
    }
    return out;
  }

  function ensureTranslateStyle() {
    var id = "__spindle_tr";
    var s = document.getElementById(id);
    if (!s) {
      s = document.createElement("style");
      s.id = id;
      document.documentElement.appendChild(s);
    }
    s.textContent =
      ".spindle-translation{opacity:0.85;}" +
      "body.sv-original .spindle-translation{display:none;}" +
      "body.sv-translation .spindle-source{display:none;}";
  }

  function applyViewClass(view) {
    document.body.classList.remove("sv-original", "sv-bilingual", "sv-translation");
    document.body.classList.add("sv-" + (view || "original"));
  }

  function startTranslation() {
    var blocks = collectLeafBlocks();
    window.__spindleBlocks = blocks;
    var payload = [];
    for (var i = 0; i < blocks.length; i++) {
      var next = blocks[i].nextElementSibling;
      if (next && next.classList.contains("spindle-translation")) continue; // already done
      payload.push({ index: i, text: (blocks[i].textContent || "").trim() });
    }
    if (payload.length && window.spindle) window.spindle.blocksCollected(JSON.stringify(payload));
  }

  function onTranslateView(view) {
    applyViewClass(view);
    if (window.spindle)
      window.spindle.currentTranslateLang(function (l) { currentLang = l || ""; });
    if (view !== "original") startTranslation();
    scheduleReapply();
  }

  function onTranslation(index, text, state) {
    var blocks = window.__spindleBlocks;
    if (!blocks || !blocks[index]) return;
    var block = blocks[index];
    var node = block.nextElementSibling;
    if (!node || !node.classList.contains("spindle-translation")) {
      node = document.createElement("p");
      node.className = "spindle-translation";
      block.insertAdjacentElement("afterend", node);
    }
    node.textContent = text;
    if (state) node.setAttribute("data-state", state);
    else node.removeAttribute("data-state");
    if (state !== "error") block.classList.add("spindle-source");
    // Translation paragraphs just appeared: re-render so translation-side marks
    // and other-side block tints attach to them.
    scheduleReapply();
  }

  // Scroll to a highlight: prefer its char-level mark, else its block tint —
  // whichever is currently visible in the active view.
  function isVisible(el) { return !!(el && el.offsetParent !== null); }

  // Jump to a highlight from the list. Highlights are only drawn on the side they
  // were made, so the precise mark may not be present in the current view; in
  // that case land on the highlight's block (the original block or its
  // translation paragraph, whichever the current view shows).
  function onScrollHighlight(id, tries) {
    tries = tries || 0;
    var target = document.querySelector('mark.spindle-hl[data-hl-id="' + id + '"]');
    if (!isVisible(target)) target = null;
    if (!target) {
      var arr = window.__spindleHighlights || [];
      var h = null;
      for (var i = 0; i < arr.length; i++) if (arr[i].id === id) { h = arr[i]; break; }
      if (h) {
        var blk = blockByNumber(h.block);
        var tr = (blk && blk.nextElementSibling &&
                  blk.nextElementSibling.classList.contains("spindle-translation"))
                   ? blk.nextElementSibling : null;
        target = isVisible(blk) ? blk : (isVisible(tr) ? tr : null);
      }
    }
    if (!target) {
      if (tries < 40) setTimeout(function () { onScrollHighlight(id, tries + 1); }, 25);
      return;
    }
    target.scrollIntoView({ block: "center", inline: "center" });
    var prev = target.style.outline;
    target.style.outline = "2px solid #f4a259";
    setTimeout(function () { target.style.outline = prev; }, 1200);
  }

  function init() {
    new QWebChannel(qt.webChannelTransport, function (channel) {
      window.spindle = channel.objects.spindle;
      window.spindle.highlightsChanged.connect(applyAll);
      window.spindle.translateViewChanged.connect(onTranslateView);
      window.spindle.translationReady.connect(onTranslation);
      window.spindle.scrollToHighlight.connect(function (id) { onScrollHighlight(id, 0); });
      ensureTranslateStyle();
      window.spindle.currentTranslateLang(function (l) { currentLang = l || ""; });
      applyAll();
      window.spindle.currentTranslateView(function (view) {
        onTranslateView(view || "original");
      });
    });
    document.addEventListener("mouseup", onMouseUp);
  }

  if (typeof QWebChannel !== "undefined" && typeof qt !== "undefined" && qt.webChannelTransport) {
    init();
  } else {
    // qwebchannel.js not ready yet; retry shortly.
    var tries = 0;
    var timer = setInterval(function () {
      if (typeof QWebChannel !== "undefined" && typeof qt !== "undefined" && qt.webChannelTransport) {
        clearInterval(timer);
        init();
      } else if (++tries > 50) {
        clearInterval(timer);
      }
    }, 20);
  }
})();
