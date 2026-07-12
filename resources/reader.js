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
  // Text nodes inside a container, in the block's *plain text* coordinate. Text
  // inside existing highlight marks IS counted (marks are transparent), so an
  // offset means the same thing whether or not other marks are currently shown —
  // essential when a block holds several highlights. (For the original side,
  // inserted translation text is still excluded.)
  function containerTextNodes(container, side) {
    var out = [];
    var walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT, {
      acceptNode: function (node) {
        var p = node.parentElement;
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
      // Bilingual rhythm: the translation hugs its original (small start
      // margin, original's end margin shrunk to match) and a clear gap
      // separates the pair from the next original. Logical properties so
      // vertical writing modes get the same spacing. Scoped to sv-bilingual —
      // translation-only view keeps the book's own block spacing.
      "body.sv-bilingual .spindle-translation{" +
      "margin-block-start:0.3em !important;margin-block-end:1.1em !important;}" +
      "body.sv-bilingual .spindle-source{margin-block-end:0.3em !important;}" +
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
      // Clone the source block (tag + attributes, no children) so the
      // translation keeps its formatting — headings render as headings,
      // styled paragraphs keep their classes/inline CSS.
      node = block.cloneNode(false);
      node.removeAttribute("id");
      node.removeAttribute("data-spindle-block"); // must not look like a source block
      node.classList.remove("spindle-source");
      node.classList.add("spindle-translation");
      if (currentLang) node.setAttribute("lang", currentLang);
      if (node.tagName === "LI")
        node.style.display = "block"; // avoid a second marker / renumbering
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

  // --- read-aloud ----------------------------------------------------------
  // Speech text + "currently reading" marker for the C++ TTS controller
  // (MainWindow drives these via runJavaScript). Indexing shares
  // window.__spindleBlocks with translation, so a speech index and a
  // translation index refer to the same block.
  function speechBlocks() {
    if (!window.__spindleBlocks) window.__spindleBlocks = collectLeafBlocks();
    return window.__spindleBlocks;
  }
  function speechNormalize(text) {
    return (text || "").replace(/\s+/g, " ").trim();
  }
  // The block's text with each <ruby> spoken as its <rt> reading — the
  // author-specified reading; reading base text + furigana concatenated
  // (plain textContent) garbles speech. A ruby without <rt> keeps its base.
  function speechTextOf(el) {
    var clone = el.cloneNode(true);
    var rubies = clone.querySelectorAll("ruby");
    for (var i = 0; i < rubies.length; i++) {
      var rts = rubies[i].querySelectorAll("rt");
      var reading = "";
      for (var k = 0; k < rts.length; k++) reading += rts[k].textContent;
      if (reading.trim()) rubies[i].textContent = reading;
    }
    return speechNormalize(clone.textContent);
  }
  // The block's finished translation node (pending/error placeholders excluded).
  function speechTrNode(block) {
    var t = block.nextElementSibling;
    if (t && t.classList.contains("spindle-translation") && !t.getAttribute("data-state"))
      return t;
    return null;
  }
  var speechMarked = null;
  function ensureSpeechStyle() {
    var id = "__spindle_speak";
    if (document.getElementById(id)) return;
    var s = document.createElement("style");
    s.id = id;
    s.textContent =
      ".spindle-speaking{background-color:rgba(244,162,89,0.22) !important;border-radius:3px;}";
    document.documentElement.appendChild(s);
  }
  window.__spindleSpeech = {
    // {count, start}: block count and the first block currently in the viewport
    // (reading starts where the user is, not at the chapter top).
    info: function () {
      var blocks = speechBlocks();
      var start = 0;
      for (var i = 0; i < blocks.length; i++) {
        var r = blocks[i].getBoundingClientRect();
        if (r.width === 0 && r.height === 0) continue;
        if (r.bottom > 0 && r.top < window.innerHeight &&
            r.right > 0 && r.left < window.innerWidth) {
          start = i;
          break;
        }
      }
      return JSON.stringify({ count: blocks.length, start: start });
    },
    // Speech text of block i. side "translation" reads the finished
    // translation; fallback=true substitutes the original text when there is
    // none (lang "" marks the result as original-language for voice choice).
    text: function (i, side, fallback) {
      var el = speechBlocks()[i];
      if (!el) return JSON.stringify({ text: "", lang: "" });
      if (side === "translation") {
        var t = speechTrNode(el);
        if (t)
          return JSON.stringify({
            text: speechNormalize(t.textContent),
            lang: t.getAttribute("lang") || currentLang || ""
          });
        if (!fallback) return JSON.stringify({ text: "", lang: "" });
      }
      return JSON.stringify({ text: speechTextOf(el), lang: "" });
    },
    // Tint + scroll to the block being spoken. Marks whichever element of the
    // original/translation pair is visible (the spoken side may be hidden in
    // the current view).
    mark: function (i, side) {
      this.clear();
      var el = speechBlocks()[i];
      if (!el) return;
      ensureSpeechStyle();
      var tr = el.nextElementSibling;
      if (!(tr && tr.classList.contains("spindle-translation"))) tr = null;
      var preferred = side === "translation" ? tr : el;
      var target = isVisible(preferred) ? preferred
                 : isVisible(el) ? el
                 : isVisible(tr) ? tr : null;
      if (!target) return;
      target.classList.add("spindle-speaking");
      speechMarked = target;
      target.scrollIntoView({ block: "center", inline: "center", behavior: "smooth" });
    },
    clear: function () {
      if (speechMarked) speechMarked.classList.remove("spindle-speaking");
      speechMarked = null;
      var stray = document.querySelectorAll(".spindle-speaking");
      for (var i = 0; i < stray.length; i++) stray[i].classList.remove("spindle-speaking");
    }
  };

  // --- image-only chapter fit ---------------------------------------------
  // Manga-style EPUBs often have a chapter that is just one full-page image.
  // When so, tag <html> so it fills the window instead of sitting inside the
  // normal reading margins (see the html.spindle-image-chapter CSS rules
  // injected from MainWindow::viewStyleCss). Runs independently of the
  // QWebChannel bridge, so it applies even before/without it.
  function applyImageFit() {
    var body = document.body;
    if (!body) return;
    var imgs = body.querySelectorAll("img, svg image");
    var text = (body.textContent || "").replace(/\s+/g, "");
    var isImageOnly = imgs.length === 1 && text.length === 0;
    document.documentElement.classList.toggle("spindle-image-chapter", isImageOnly);
  }

  // --- image-only chapter drag panning ------------------------------------
  // Once zoomed beyond fit-to-window (see the A-/A+ zoom on
  // html.spindle-image-chapter in MainWindow::viewStyleCss) the page
  // overflows and scrolls; let the user drag with the mouse to pan it, like
  // an image viewer, instead of hunting for scrollbars. Scoped to image-only
  // chapters (see applyImageFit) so ordinary text chapters keep normal
  // click-drag text selection.
  var panState = null;
  function isImageChapter() {
    return document.documentElement.classList.contains("spindle-image-chapter");
  }
  // The element whose scrollLeft/Top actually moves the page. NOT simply
  // document.body: in standards mode, body's overflow:auto propagates to the
  // viewport and body's own used overflow becomes `visible`, making
  // body.scrollLeft a silent no-op — the real scroller is then
  // document.scrollingElement (<html>). Probe for whichever element genuinely
  // has scrollable overflow right now.
  function panScroller() {
    var candidates = [
      document.scrollingElement || document.documentElement,
      document.body
    ];
    for (var i = 0; i < candidates.length; i++) {
      var el = candidates[i];
      if (el && (el.scrollWidth > el.clientWidth || el.scrollHeight > el.clientHeight))
        return el;
    }
    return null; // fit-to-window (zoom <= 100%): nothing to pan
  }
  function onImagePanStart(e) {
    if (!isImageChapter() || e.button !== 0) return;
    var el = panScroller();
    if (!el) return;
    panState = { el: el, x: e.clientX, y: e.clientY,
                 scrollLeft: el.scrollLeft, scrollTop: el.scrollTop };
    document.body.style.cursor = "grabbing";
    e.preventDefault(); // suppress native image drag-out and text selection
  }
  function onImagePanMove(e) {
    if (!panState) return;
    panState.el.scrollLeft = panState.scrollLeft - (e.clientX - panState.x);
    panState.el.scrollTop = panState.scrollTop - (e.clientY - panState.y);
  }
  function onImagePanEnd() {
    if (!panState) return;
    panState = null;
    document.body.style.cursor = "";
  }
  document.addEventListener("mousedown", onImagePanStart);
  document.addEventListener("mousemove", onImagePanMove);
  document.addEventListener("mouseup", onImagePanEnd);
  document.addEventListener("dragstart", function (e) {
    if (isImageChapter()) e.preventDefault();
  });

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

  applyImageFit();

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
