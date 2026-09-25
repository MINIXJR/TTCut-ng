"""Add the 2026-09-25 run-8 verdicts (audit run 8, scope: the 28 source files
of docs/code-map/track-management.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-25-run8.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run8 and must hold
candidates.tsv; rescan-dir defaults to code-audit-run8c, the rescan after the
batches (run8b was the rescan before the last const fix). Both stay out of the repository on purpose.

Two subagents (sonnet: data/, and gui+common) classified the 62 never-judged
in-scope candidates against the map; the main session verified the gui
consolidate and the open-task clone, spot-checked one deliberate per module.
First run under the rule "offen darf nicht offen bleiben"
(docs/quality-roadmap.md): the 42 open in-scope items of earlier runs were
each put in one drawer - rebuilt in this run, own project in TODO.md
("Umbau-Projekte aus den Code-Audits", P1-P7, stored as documented), or
deliberate - by user decision of 2026-09-25. Two of them carried a wrong
"done ... run 3" text; the code had not changed."""
import csv, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run8")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run8c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-25"

# fingerprint -> (verdict, reason); one entry per new in-scope candidate.
RULINGS = {
 '4a59de0a8bf4b36f46ee84e9f31dfc837d81980e': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - TTOpenAudioTask/TTOpenSubtitleTask are one class modulo type names, the valid-type check and the finished() signal type; target: shared non-QObject open helper (Qt signals cannot be templated)'),
 '5410bc3547effeb6c6b94dd13ac9d447bf38a188': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - track-management.md redundancy "Two list classes with one shape": one template list with per-kind sort/order-default policy (TTAudioList/TTSubtitleList declarations)'),
 '94d576d41df988d90f2c903d027fe4f0efdcbfde': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - append/remove bodies identical in TTAudioList, TTMarkerList, TTSubtitleList; same template-list target, widened to TTMarkerList'),
 'b8510c278d4d401221d42e98c8dec43c2180057a': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - TTAudioItem/TTSubtitleItem field and constructor declarations; item half of the template-list target'),
 '6a80fcc2ff5db683891b80aa82869dcf3d3986a8': ('deliberate',
  'banner + size guard + qWarning shape shared by parseCutSection/parseMarkerSection with different child counts and unrelated downstream logic'),
 '9e31d90f6197c9e51f7d8338e1c68880ba966aaa': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - operator==/stream/fileName/length accessors identical in TTAudioItem/TTSubtitleItem; template-list target'),
 'ccd1e90458aeaed548c25d21ae7d972ffa1150e6': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - setItemData tail (langFromFilename) + operator= in both item classes; template-list target'),
 '478685eba69e5d87af8074baf3d5ff7b0a2b72bc': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - two-overload append in TTMarkerList/TTSubtitleList incl. the order-default branch (the H2 divergence); template-list target'),
 '64b8acd9f95229108037b7b98c95ee69d5ad8e0b': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - copy constructor + setItemData opening in both item classes; template-list target'),
 'de35eebee698f24dcd97f22eb1fab0dda5157f5d': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - operator= tail + operator< head in both item classes; template-list target with the sort as policy hook'),
 '292798ee1d1e46e63dbd296601aed150c127f0f0': ('consolidate',
  'done 2026-09-25 audit run 8 batch A1: TTCutProjectData::parseTrackLanguageDelay shared by parseAudioSection and parseSubtitleSection'),
 'c783bc3fc70d9eee2d74bf8d6a76fa6ce2c80bda': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - constructor tail + copy-constructor head in both item classes; template-list target'),
 'a7b4795978ac050274cff8e2e981a1c6d0f742c1': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - TTCutItem/TTSubtitleItem operator=/operator< (mOrder compare) identical; template-list target widened to TTCutItem (cut-edit-and-start.md: no common base yet)'),
 '945689ae8f334597ca936dec00375d9d2271b9be': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - indexOf/count/swap delegations of TTCutList and TTSubtitleList; template-list target widened to TTCutList'),
 '725226f22a01150350c9ca11fd99205b5446a40e': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - update/at/count: TTAudioList::update has no index guard, TTMarkerList::update has one; template-list target picks the guarded contract'),
 '189f00a482130f02f3e0e6cf2b597659ed58f3ee': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - operation() tail of the two open tasks; same target as the open-task clone'),
 '7173ae0df8e4e6fa8660fff158a9f6d2f64763df': ('deliberate',
  'two-line DOM toInt read idiom repeated across the parser; no helper worth it'),
 '60180fdc73d6d751e81acfe41bcf849673d7bb66': ('documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage), decided 2026-09-25 audit run 8 - constructor/copy-constructor boundary of both item classes; template-list target'),
 'f29e4d0af54ddf628fbb8b239b841968c3c6d01a': ('documented',
  'docs/conventions.md cpp/header_comment: older /*! banner forms remain in place (legacy TTCut file)'),
 '6db5f51f0b4918e300c1dbeaf503a099c4497d39': ('deliberate',
  'TTAVList::indexOf(TTAVItem*) backs QList<TTAVItem*>::indexOf; const pointer would not match the storage, whole class uses non-const pointers'),
 'b1103be4d3d3b05dada854fb4496ae15f5258fde': ('consolidate',
  'done 2026-09-25 audit run 8 batch B3: canCutWith runs checkStreamCompat, checkMpeg2SequenceCompat, checkAudioCompat'),
 '67da5edd7f1daa1adda6f15bbd71f6cf46d780a6': ('deliberate',
  'TTAudioItem getters are all by value (5 of 7 compute); by-ref for two would break local consistency; QString is COW'),
 'caff603afe84c113ef5b43549610a2fe93240d4b': ('deliberate',
  'same by-value getter consistency in TTSubtitleItem'),
 'f0321e573da927295718f185dade55bfa36a3eeb': ('deliberate',
  'same by-value getter consistency (TTAudioItem::getLength)'),
 'd6268143df12c9cc15f42600b9efa79c7b7605c6': ('deliberate',
  'same by-value getter consistency (TTSubtitleItem::getLength)'),
 '7aa8b55a1f7d93cb1e863d0eccd239aa6b52b5c7': ('consolidate',
  'done 2026-09-25 audit run 8 batch B2: dead store removed'),
 'c3084866b175887dd7a27bc13236633c88ede11e': ('consolidate',
  'done 2026-09-25 audit run 8 batch B2: dead store removed'),
 '698111dae0c066dbdc7349d29f67a3ad18ff5fa1': ('deliberate',
  'legacy TTCut constructors assign every field in the body; conventions.md: existing code is not reformatted piecemeal'),
 '1e81ea58164b1ed59ad9829872437063acbf1cc9': ('deliberate',
  'same legacy constructor style (TTAudioItem default constructor)'),
 '2badd9cdf69893fc7e6dc30d35db1e1fecc8d324': ('deliberate',
  'same legacy constructor style (TTAudioItem copy constructor)'),
 '4f0d97545d31a812c6861c8d217fd4608ba117bb': ('deliberate',
  'same legacy constructor style (TTSubtitleItem default constructor)'),
 '28a6783b238119edb88d20693f63ec7476bfc26d': ('deliberate',
  'same legacy constructor style (TTSubtitleItem copy constructor)'),
 'ac006dc22e01f46c89bd483227f1bf223d94d40b': ('deliberate',
  'same legacy constructor style (TTSubtitleItem copy constructor, subtitleLength)'),
 'd0497b607d9b5cff3f863a8d6979c75025f4807c': ('deliberate',
  'generic connect(sender,&C::sig,this,&C::slot) wiring shape: pool task signals (wireTask/disconnectTaskSignals) vs unrelated TTCutMainWindow UI wiring; coincidental token match'),
 'f6f7d9e2a889f17b1a880c3dd8e7436b627c7c75': ('deliberate',
  'per-group QSettings load blocks in TTSettings::load(), different groups/keys/types; the file documents the per-group layout pinned to the key inventory'),
 '530e2e60a9e3ce25ce0fa6ebb8bd1710f340335c': ('deliberate',
  'TTSettings setter guard+assign idiom, documented in ttsettings.cpp ("Each setter early-outs on no-op assignment"); unrelated fields'),
 '8b2d086e50ae503f7bfee63e8b8a2f9e78523437': ('deliberate',
  'same documented TTSettings setter idiom (Common vs H264/H265 codec setters)'),
 '404812e3fbbf0bb4cd0775ed267746a7402ed8db': ('deliberate',
  'subset of the per-group QSettings load blocks, same reasoning'),
 'ffd21e381ad71154bcabecd14314c403c1653c52': ('deliberate',
  'TTSettings setter idiom ("The other 5 setters use the standard pattern"), unrelated field groups'),
 '23de16ca50f4c074ef0f931b9111c493c2219c91': ('deliberate',
  'TTSettings setter idiom, navigation vs logging setters'),
 '8c7537962c2b591b1eeedbb44607a044a82d80ca': ('deliberate',
  'lazy-init static QMap idiom holding two unrelated tables (iso639_1to2 base map vs normalizeLangCode alias table)'),
 '51231a885bcc91199b884422adcb96bf64dd5690': ('deliberate',
  'TTSettings setter/load-group idiom, unrelated fields'),
 '39b9c60b224d702e572e73c2e7d8238cde2701df': ('deliberate',
  'languageCodes()/languageNames() are two index-aligned tables of different content consumed together by populateLanguageCombo (H5 context)'),
 '3715f6da22920854085be91818ebc6d9284fefc4': ('deliberate',
  'disconnectTaskSignals vs onOpenProjectFileAborted disconnect block: unrelated signal sets, coincidental shape'),
 '60250bfad5e5415a24a13bf8f7ad896892615ba5': ('documented',
  'track-management.md redundancy "View wiring": kept separate, TTTrackTreeView class comment keeps model wiring per subclass because the signal sets differ'),
 '22013bef57a70eeef5dc8286f4d9529105889a6c': ('documented',
  'onSwapItems/onReloadList of the same track-management.md "View wiring" entry'),
 '071c65f60a0bc7b4d2ec7b33594fda4694bf7092': ('deliberate',
  'generic setupUi + one connect constructor idiom (about dialog vs settings pages), unrelated'),
 '06accf9c4228258fad13b1697eb9dd9295aa7f94': ('deliberate',
  'setTabData/saveTabData shape shared by every settings page (settings-state.md), fields differ per page'),
 '3bbb01094ec13ec8444dc175a83e84b9802934b8': ('documented',
  'header side of the track-management.md "View wiring" entry (kept separate)'),
 '0c9e463acd6195eefd219205f365488429e2ec55': ('deliberate',
  'four ttMakeAction+connect pairs for four different actions inside bindListWidgets'),
 '160150d69a01318ce87c109d629fdd1af12c962d': ('documented',
  'per-subclass constructor (own .ui, column widths, bindListWidgets texts) documented in the TTTrackTreeView class comment'),
 'd34897a67bc5f6eb058626dbe140897261ea3199': ('deliberate',
  'generic QTreeWidgetItem setText population idiom, audio columns vs cut-list columns'),
 'c47c6a2d594d4abced724f678b75d27323ed7abf': ('deliberate',
  'addDelaySpin vs addLanguageCombo row-lookup lambdas: two widget types, two signals'),
 'b81a132cf13b30812add90bd7bbf89834527911f': ('deliberate',
  'generic setText population idiom (audio row vs cut-list update), unrelated columns'),
 '6979be940d812bc1a2c2184c4674e1910aeb2aa1': ('deliberate',
  'subset of the settings-page constructor idiom'),
 '715528dca5e8de0b993a96cf4bb665c540eb09e7': ('deliberate',
  'overlapping range of the QTreeWidgetItem population idiom'),
 'f0db1908bff571de6e505334e634adcf33628aec': ('deliberate',
  'goto-frame dialog connects vs bindListWidgets connects: unrelated widgets, generic multi-connect shape'),
 '124ed74018fd86248ecd33fa48a387ea06bbb271': ('deliberate',
  'onOpenProjectFileAborted vs TTCutSettingsMuxer constructor tail: coincidental two-call shape'),
 '64b13a962c69b6b3b5de3b0153a10d0f516c2cce': ('deliberate',
  'closeProject/navigationEnabled vs TTCutTreeView controlEnabled: repeated setEnabled calls on unrelated widget sets'),
 'fefaae2b08338162711ff2851df94280513743d5': ('consolidate',
  'done 2026-09-25 audit run 8 batch B1: ttThemeIcon removed, tttreeviewutil.h includes ttthemedicon.h, nine call sites use ttThemedIcon'),
 'b0d44fed2afe9076247a45141725495ba4084788': ('documented',
  'docs/conventions.md cpp/header_comment: the older /*!, /** and /* //// */ banner forms remain in place'),
 'd76862ec00c1bdc19f644ced83f438f960895853': ('deliberate',
  'the unprefixed names are fields of the plain parameter struct ActionTexts; real members carry m/mp; same practice as TTMuxTaskParams, SearchControls, HintCell'),
}

# fingerprint -> (kind, verdict, reason); the 42 open in-scope items of earlier
# runs, sorted into the three drawers.
OPEN_RULINGS = {
 '47cb4a2676c76002216078b9a5b80b41572e5b62': ('clone', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P2 TTESInfo aufraeumen (open-item ruling 2026-09-25, audit run 8)'),  # clone x2 (avstream/ttesinfo.cpp:257-269)
 '09f0008618870b2f12b9023db0d0a2fb1194c908': ('clone', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P2 TTESInfo aufraeumen (open-item ruling 2026-09-25, audit run 8)'),  # clone x3 (avstream/ttesinfo.cpp:311-319)
 'c98519701db4f2a2a6ddce62d892bd8df3c7cecb': ('clone', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P2 TTESInfo aufraeumen (open-item ruling 2026-09-25, audit run 8)'),  # clone x2 (avstream/ttesinfo.cpp:320-336)
 '53851f24b28975e0683f2c2025b1eef8eaddd458': ('clone', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P2 TTESInfo aufraeumen (open-item ruling 2026-09-25, audit run 8)'),  # clone x2 (avstream/ttesinfo.cpp:306-310)
 'fc4efbef53561ff46a6657818d0f152ebb5b2cad': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P2 TTESInfo aufraeumen (open-item ruling 2026-09-25, audit run 8)'),  # function 'parseWarningsSection' has cognitive complexity of 
 '1a4ad6152deec2d3e619baccc47567760e200b52': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - TTSettings::load, symmetric commented group blocks (settings-state.md)'),  # function 'load' exceeds recommended size/complexity threshol
 'd8804b090e0e286e3a5c8f2417db2f67a11183d1': ('clone', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P3 Vorschau-Pipeline (open-item ruling 2026-09-25, audit run 8)'),  # clone x2 (data/ttcutpreviewtask.cpp:575-581)
 '4afc49c029cbdb8039f3426b38654b764793137c': ('clone', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8; the run-3 "done" entry was wrong): MPEG-2 and H.26x cut starts build different stage plans (order, calibration keys, cut count); what they share is the announceCutStart call itself'),  # clone x2 (data/ttavdata.cpp:1777-1786)
 'e2b67feac66d07229a0823aad0732f021d8ee8be': ('clone', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P4 Extra-Frame-Dialog in TTAVData (open-item ruling 2026-09-25, audit run 8)'),  # clone x2 (data/ttavdata.cpp:618-621)
 '5f3999241543515d44ff1c18b5dc2dcfd249bd18': ('clone', 'consolidate',
  'done 2026-09-25 audit run 8 batch A1: one local disableRepair helper for the three disabled-repair warnings in parseAudioSection'),  # clone x2 (data/ttcutprojectdata.cpp:343-351)
 '803070954dfb892ec3b62f1cadb8bd884f0fa3b1': ('clone', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P4 Extra-Frame-Dialog in TTAVData (open-item ruling 2026-09-25, audit run 8)'),  # clone x2 (data/ttavdata.cpp:404-408)
 '2962c033ed67618cf8586e98a7dd3b85ae1c5e1f': ('clone', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P4 Extra-Frame-Dialog in TTAVData (open-item ruling 2026-09-25, audit run 8)'),  # clone x2 (data/ttavdata.cpp:411-417)
 'ec093e81190243ad94f85d6d8cdbcaa7b29aa298': ('clone', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P3 Vorschau-Pipeline (open-item ruling 2026-09-25, audit run 8)'),  # clone x2 (data/ttcutpreviewtask.cpp:482-485)
 '3acf948d31138d6ce0f759929cd66edfdbde7465': ('method-size', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P5 TTAVData::cutAudioTracks zerlegen (open-item ruling 2026-09-25, audit run 8)'),  # TTAVData::cutAudioTracks
 'ea2a29ecde66898a2d99aa36faf475e1e4ae6fc7': ('method-size', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P4 Extra-Frame-Dialog in TTAVData (open-item ruling 2026-09-25, audit run 8)'),  # TTAVData::showExtraFrameClusterDialog
 'b1438c3b5597a0126b713cbc8612e32553801034': ('tool', 'consolidate',
  'done 2026-09-25 audit run 8 batch B2 (the run-3 "done" entry was wrong, the line was unchanged): cutWarnings reads the AV item as const'),  # constVariablePointer: Variable 'avItem' can be declared as p
 '8750f3c13502204aae775802644a4cf909e2fb56': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P3 Vorschau-Pipeline (open-item ruling 2026-09-25, audit run 8)'),  # function 'createH264PreviewClip' exceeds recommended size/co
 '7003bcb99991a81d0002bead7c72e308c9239791': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P3 Vorschau-Pipeline (open-item ruling 2026-09-25, audit run 8)'),  # function 'createH264PreviewClip' has cognitive complexity of
 '89c7dc2bbdfd134a87a895b830221d619029db7b': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P5 TTAVData::cutAudioTracks zerlegen (open-item ruling 2026-09-25, audit run 8)'),  # function 'cutAudioTracks' exceeds recommended size/complexit
 'dd4089e8aca0d6fee4ea33370d4786af338331dd': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P5 TTAVData::cutAudioTracks zerlegen (open-item ruling 2026-09-25, audit run 8)'),  # function 'cutAudioTracks' has cognitive complexity of 83 (th
 '1c6eff9995ede1ac8d9fff15eaae5425f9f97c18': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - deserializeStreamPoints, one tag dispatch (stream-points.md)'),  # function 'deserializeStreamPoints' has cognitive complexity 
 'de9e0af2ec3b5d6697f9ec9072cec28cdf51b6b9': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - loadExtraFrameIndices, MPEG-2 parser vs .info precedence (audio-cut-timing.md)'),  # function 'loadExtraFrameIndices' has cognitive complexity of
 '279ec479dd987a88a5036d973023bd0d6d7d245b': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - onCutFinished, container/mux/chapter/ES-deletion branches (settings-state.md, output-mux.md)'),  # function 'onCutFinished' has cognitive complexity of 35 (thr
 '898a0013c5808475be6b514b226df8e63668bf91': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - onDoCut, audio+subtitle then pool start (audio-cut-timing.md, cut-edit-and-start.md)'),  # function 'onDoCut' exceeds recommended size/complexity thres
 '80195676c6dcd1aa2498aeead622724b98ecddf0': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - onDoCut complexity, same function'),  # function 'onDoCut' has cognitive complexity of 30 (threshold
 'e9cca98c4a5b93bd0d3b954c12ec00fb27aa2aba': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - onOpenVideoFinished, step order in stream-open-project-load.md'),  # function 'onOpenVideoFinished' has cognitive complexity of 5
 'ece69955946f7c123f693df45400441174ec4382': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - openAVStreams, phases in stream-open-project-load.md and track-management.md'),  # function 'openAVStreams' has cognitive complexity of 85 (thr
 '84db05c0ff56ab6b24aef9048926ed6cbf8498aa': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P3 Vorschau-Pipeline (open-item ruling 2026-09-25, audit run 8)'),  # function 'operation' exceeds recommended size/complexity thr
 '29a6357b2219fa110432ace09022d7d863697442': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P3 Vorschau-Pipeline (open-item ruling 2026-09-25, audit run 8)'),  # function 'operation' has cognitive complexity of 86 (thresho
 '565b1414b461f888d72ef89100b1391397a0e422': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - parseAudioSection after run 8 moved the language/delay loop and the repair warnings into helpers'),  # function 'parseAudioSection' has cognitive complexity of 36 
 '8dd79cae1661a8b06e1ec53c31a5d395b2d01770': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - parseSettingsSection, one chain over the three value classes (settings-state.md)'),  # function 'parseSettingsSection' has cognitive complexity of 
 '2417b9ad1d556dfe02b73bcb3f7a2496b47f1381': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - TTH26xCutTask::runCut, engine checks/progress/naming moved out in run 7 batch B4; the rest is the documented cut sequence (output-mux.md)'),  # function 'runCut' exceeds recommended size/complexity thresh
 '99e69ec610a0bc80b51b136fa9dd0d95dccc5910': ('tool', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): a linear sequence whose step order a code map records; splitting it would move lines without removing duplication - runCut complexity, same function'),  # function 'runCut' has cognitive complexity of 48 (threshold 
 'aadff2187696e719aa841e4cec36b85e0ac3e14d': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P4 Extra-Frame-Dialog in TTAVData (open-item ruling 2026-09-25, audit run 8)'),  # function 'showExtraFrameClusterDialog' exceeds recommended s
 'c014af376eb630b4dfbd3665be568c79d052e010': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P4 Extra-Frame-Dialog in TTAVData (open-item ruling 2026-09-25, audit run 8)'),  # function 'showExtraFrameClusterDialog' has cognitive complex
 'eeea200fde9bdb36678dbb5685210f8f36f8e6a9': ('tool', 'consolidate',
  'done 2026-09-25 audit run 8 batch B2: doH264Cut declares the logged audio file name inside its branch'),  # variableScope: The scope of the variable 'audioFile' can be 
 '7524c5c127c0db45c1be6a081a7efaf04472d48f': ('class-size', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P1 TTCutMainWindow-Rest (open-item ruling 2026-09-25, audit run 8)'),  # TTCutMainWindow
 '7cf698f4e25afcc9f23a0fb5af540d7a17cd2d6d': ('clone', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): the two TTCurrentFrame cut-in slots end with the same short call sequence; a helper would only name those calls'),  # clone x2 (gui/ttcurrentframe.cpp:370-386)
 '14b061ad717268641f9ac4f1ed4903da5919fa63': ('clone', 'deliberate',
  'deliberate (open-item ruling 2026-09-25, audit run 8, user decision): cut dialog and main window connect blocks share the generic multi-connect shape, not logic'),  # clone x2 (gui/ttcutavcutdlg.cpp:58-65)
 'f489c77cf0d23131bfa03de674a727cc94c6bd50': ('file-size', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P1 TTCutMainWindow-Rest (open-item ruling 2026-09-25, audit run 8)'),  # gui/ttcutmainwindow.cpp
 '6d38aee8b9a572685c5eeb61cd30a44e724834e0': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P1 TTCutMainWindow-Rest (open-item ruling 2026-09-25, audit run 8)'),  # function 'onStatusReport' has cognitive complexity of 47 (th
 '336061f382a677f20ee47ab8b8c2a25d864b3dcc': ('tool', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P1 TTCutMainWindow-Rest (open-item ruling 2026-09-25, audit run 8)'),  # function 'onStreamPointsLoaded' has cognitive complexity of 
}

# The scope as it stood when the run started (map sources of track-management.md).
RUN_SCOPE = ['data/ttaudiolist.h', 'data/ttaudiolist.cpp', 'data/ttsubtitlelist.h', 'data/ttsubtitlelist.cpp', 'data/ttavlist.h', 'data/ttavlist.cpp', 'data/ttavdata.h', 'data/ttavdata.cpp', 'data/ttopenaudiotask.cpp', 'data/ttopensubtitletask.cpp', 'data/ttcutprojectdata.cpp', 'data/tth26xcuttask.cpp', 'data/ttcutpreviewtask.cpp', 'gui/tttracktreeview.h', 'gui/tttracktreeview.cpp', 'gui/tttreeviewutil.h', 'gui/ttaudiotreeview.h', 'gui/ttaudiotreeview.cpp', 'gui/ttsubtitletreeview.h', 'gui/ttsubtitletreeview.cpp', 'gui/ttcutmainwindow.cpp', 'gui/ttcurrentframe.cpp', 'gui/ttcutsettingsaudio.cpp', 'common/ttcut.h', 'common/ttcut.cpp', 'common/ttsettings.h', 'common/ttsettings.cpp', 'avstream/ttesinfo.cpp']
def in_scope(row, files=RUN_SCOPE):
    return any(loc.split(":")[0] in files for loc in (row.get("location") or "").split(";"))

store = vd.load(OUT)
stats = Counter()

rows = [r for r in csv.DictReader((SCAN / "candidates.tsv").open(), delimiter="\t")
        if r["status"] == "new" and in_scope(r)]
seen = set()
for r in rows:
    if r["fingerprint"] not in RULINGS:
        raise SystemExit(f"in-scope candidate without a ruling: {r['name']}")
    verdict, reason = RULINGS[r["fingerprint"]]
    store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
    seen.add(r["fingerprint"])
    stats["new " + verdict] += 1
unused = set(RULINGS) - seen
if unused:
    raise SystemExit(f"{len(unused)} rulings match no candidate")

for fp, (kind, verdict, reason) in OPEN_RULINGS.items():
    if fp not in store:
        raise SystemExit(f"open ruling for a fingerprint not in the store: {fp}")
    store[fp] = vd.Verdict(fp, kind, verdict, D, reason[:400])
    stats["open -> " + verdict] += 1

# The rescan after the batches: code the run itself moved shows up as new;
# RESCAN_RULINGS (filled after the rescan) judges each of those.
RESCAN_RULINGS = {
 '192968c85cd7123b316ca1892e1643db7dc4f998': ('deliberate',
  'deliberate (rescan after run 8): connect-statement idiom (95 sites project-wide), a Qt wiring shape, not a consolidation target'),
 'ca9651246b2c43b0f91bb5d089e6016d848ee093': ('deliberate',
  'deliberate (rescan after run 8): generic connect/disconnect wiring shape; the new sites are the subtitle/audio hooks this run added to TTCutMainWindow::onAVItemChanged, unrelated signal sets'),
 'a64b40f135bf9bed7d01ba38b38ae7645e33812e': ('deliberate',
  'deliberate (rescan after run 8): same documented TTSettings setter idiom as the run-8 rulings on common/ttsettings.cpp ("Each setter early-outs on no-op assignment"); the class only shifted because setAudioLanguagePreference lost its emit'),
 'f43c97fad9d31fca23c52560c1c7d08b6f918b3c': ('deliberate',
  'deliberate (rescan after run 8): same documented TTSettings setter idiom as the run-8 rulings on common/ttsettings.cpp ("Each setter early-outs on no-op assignment"); the class only shifted because setAudioLanguagePreference lost its emit'),
 '4b03da155918c41ff4145b22d6197732707e5ef0': ('deliberate',
  'deliberate (rescan after run 8): same documented TTSettings setter idiom as the run-8 rulings on common/ttsettings.cpp ("Each setter early-outs on no-op assignment"); the class only shifted because setAudioLanguagePreference lost its emit'),
 'dbfb99d50debc233e4fd473d16964b9cc164682a': ('documented',
  'documented (rescan after run 8): TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage); the clone grew because TTSubtitleList gained sortByProjectOrder/order default like TTAudioList in batch A1'),
 '6d72ca9407063022a33a116aece2372a1acfdc50': ('deliberate',
  'deliberate (rescan after run 8): generic connect/disconnect wiring shape; the new sites are the subtitle/audio hooks this run added to TTCutMainWindow::onAVItemChanged, unrelated signal sets'),
 'dbe72ad9612a03ad3b5c2eafdc940684fe709495': ('deliberate',
  'deliberate (rescan after run 8): generic connect/disconnect wiring shape; the new sites are the subtitle/audio hooks this run added to TTCutMainWindow::onAVItemChanged, unrelated signal sets'),
 'b87c4edf3a7f8e1fb3cf2c682ce591cb991cf67f': ('documented',
  'documented (rescan after run 8): TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage); the clone grew because TTSubtitleList gained sortByProjectOrder/order default like TTAudioList in batch A1'),
 'cd3fb30da11db245b16abfd5f7f8da8d30d86c94': ('documented',
  'documented (rescan after run 8): TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage); the clone grew because TTSubtitleList gained sortByProjectOrder/order default like TTAudioList in batch A1'),
 '326170cb6844fcf6a5ca1ef311654847dbfa273f': ('deliberate',
  'deliberate (rescan after run 8): generic connect/disconnect wiring shape; the new sites are the subtitle/audio hooks this run added to TTCutMainWindow::onAVItemChanged, unrelated signal sets'),
 'bd6d9a740fd3225774ef38552b9c7dd6720fbb3a': ('documented',
  'documented (rescan after run 8): TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage); the clone grew because TTSubtitleList gained sortByProjectOrder/order default like TTAudioList in batch A1'),
 '2779302e4900933e32c49951959ffa4ea6b300e0': ('documented',
  'documented (rescan after run 8): TODO.md "Umbau-Projekte aus den Code-Audits" P6 (Spur-Listen als eine Vorlage); the clone grew because TTSubtitleList gained sortByProjectOrder/order default like TTAudioList in batch A1'),
 '46764dc0cf2d5082cd8a0fd70f57c24c6798f2a9': ('documented',
  'documented (rescan after run 8): TODO.md P6 - the parallel audio/subtitle finish handlers in TTAVData (pending values applied, project-order sort) follow the two list classes and merge with them'),
 '7d8fd70c22ea093d3167e664ba4701b7e77881e1': ('deliberate',
  'deliberate (rescan after run 8): one-line setPending* setters, one per map; nothing to share beyond the insert'),
 '74cb8abce0259700c4cde21be822bcadf7dfcd1c': ('documented',
  'documented (rescan after run 8): TODO.md P6 - the parallel audio/subtitle finish handlers in TTAVData (pending values applied, project-order sort) follow the two list classes and merge with them'),
 '2832365058e949795a1a02db8e8e834e86807490': ('deliberate',
  'deliberate (rescan after run 8): generic connect/disconnect wiring shape; the new sites are the subtitle/audio hooks this run added to TTCutMainWindow::onAVItemChanged, unrelated signal sets; the tree-view sites are the documented per-subclass view wiring (track-management.md "View wiring")'),
 'e2c091851e491f9daa9048cb9c7692ce576d8d6f': ('deliberate',
  'deliberate (rescan after run 8): generic connect/disconnect wiring shape; the new sites are the subtitle/audio hooks this run added to TTCutMainWindow::onAVItemChanged, unrelated signal sets'),
 '4035c14f4de0fe641ce9e1098b20bba1897d4ad0': ('deliberate',
  'deliberate (rescan after run 8): generic connect/disconnect wiring shape; the new sites are the subtitle/audio hooks this run added to TTCutMainWindow::onAVItemChanged, unrelated signal sets (cut preview player wiring on the other side)'),
}
if RESCAN.exists():
    for r in csv.DictReader((RESCAN / "candidates.tsv").open(), delimiter="\t"):
        if r["status"] != "new" or not in_scope(r):
            continue
        if r["fingerprint"] not in RESCAN_RULINGS:
            raise SystemExit(f"rescan candidate without a ruling: {r['name']}")
        verdict, reason = RESCAN_RULINGS[r["fingerprint"]]
        store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
        stats["rescan " + verdict] += 1

vd.save(OUT, store)
print(dict(stats))
