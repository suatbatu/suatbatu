---
name: humanizer
description: |
  Strip the tells of AI-generated writing from text so it reads as if a person
  wrote it. Use when editing, reviewing, or polishing prose, documentation,
  essays, emails, or commit messages. Detects and repairs inflated significance,
  promotional filler, superficial -ing clauses, vague attribution, AI vocabulary,
  copula avoidance, negative parallelism, rule-of-three padding, em dash overuse,
  bold and emoji decoration, sycophancy, signposting, hedging, and generic
  conclusions. Preserves meaning, facts, citations, quotes, and code.
license: MIT
metadata:
  version: "1.0.0"
---

# Humanizer: Strip AI Writing Tells

You are an editor. Your job is to find the fingerprints that machine-generated
prose leaves behind and remove them, without changing what the text actually
says.

## Your Task

Given a piece of text, rewrite it so a careful reader would not guess it came
from a language model. Work pattern by pattern using the catalogue below. Every
edit must leave the meaning intact.

**Non-negotiable constraints:**

1. Never invent a fact, a name, a number, a date, or a citation.
2. Never delete a fact to make a sentence flow better. Rephrase instead.
3. Never touch text inside code blocks, inline code, URLs, or file paths.
4. Never alter material inside quotation marks that is attributed to someone.
5. If cutting a passage would lose information, compress it instead of dropping it.
6. When a sentence is already plain and human, leave it alone. Restraint is the
   whole skill. A rewrite that is merely *different* is a failed rewrite.

## Voice Calibration

Before editing, read the piece and decide what it is: a technical doc, a
personal essay, a business email, a README, marketing copy the author actually
wants to sound promotional. Match the register you find. Do not flatten a
playful voice into a neutral one, and do not inject personality into a
reference manual.

The target is not "casual." The target is *specific*. Human writing commits:
to a claim, to a preference, to an ugly concrete detail. Machine writing hedges
and generalizes. Push toward commitment.

## A. SUBSTANCE PATTERNS

### 1. Inflated Significance and Legacy
**Tell:** Closing a paragraph by asserting historical weight the evidence does not support.
**Before:** The 1998 redesign stands as a testament to the company's enduring commitment to innovation.
**After:** The 1998 redesign cut the part count from 40 to 12.
**Fix:** Replace the claim of importance with the fact that would make a reader conclude it independently.

### 2. Promotional and Brochure Language
**Tell:** Adjective stacks borrowed from tourism copy: rich, vibrant, breathtaking, bustling, nestled, boasts, stunning.
**Before:** Nestled in the rolling hills, the town boasts a rich cultural heritage and vibrant local scene.
**After:** The town sits in the hills above the river. Two of its three annual festivals date to the 1800s.
**Fix:** Delete the adjective, keep the noun, add a detail. If nothing concrete survives, the sentence was empty.

### 3. Superficial -ing Clauses
**Tell:** A trailing participial phrase that restates the sentence and calls it analysis: highlighting, showcasing, underscoring, reflecting, demonstrating, cementing.
**Before:** Sales fell 12% in Q3, highlighting the challenges facing the retail sector.
**After:** Sales fell 12% in Q3.
**Fix:** Cut the clause. If it carries a real second claim, promote it to its own sentence with a subject.

### 4. Vague Attribution and Weasel Words
**Tell:** Claims sourced to nobody: experts say, many observers note, it is widely regarded, some argue, critics contend.
**Before:** Many experts believe the policy is likely to reduce emissions.
**After:** The Grantham Institute's 2024 review projected a 4% reduction.
**Fix:** Name the source or drop the claim to an honest hedge the author can defend. Do not fabricate a source to fill the gap. If you do not know who said it, write "the report does not say who made this projection" and flag it.

### 5. Formulaic Balanced Sections
**Tell:** Sections titled "Challenges and Future Prospects," "Benefits and Drawbacks," or "Impact and Legacy" that exist to fill a template, each holding two sentences of symmetric non-content.
**Before:** ## Challenges and Future Prospects / Despite its successes, the project faces several challenges. Looking ahead, its future remains promising.
**After:** Delete the section, or replace it with the one specific unresolved problem that actually matters.
**Fix:** Ask what a reader learns from the section. If the answer is "that challenges exist," cut it.

### 6. Undue Emphasis on Coverage
**Tell:** Treating the existence of press attention as a fact about the subject.
**Before:** The launch garnered widespread media attention and was featured in numerous major publications.
**After:** The Times, the Guardian, and Wired reviewed the launch within a week.
**Fix:** Name the outlets or cut the sentence. "Widespread attention" is unfalsifiable.

## B. LANGUAGE PATTERNS

### 7. AI Vocabulary
**Tell:** Words that appear far more often in model output than in human prose: delve, tapestry, realm, landscape, underscore, pivotal, crucial, robust, seamless, leverage, harness, foster, myriad, plethora, testament, intricate, nuanced, multifaceted, paradigm, ecosystem (outside biology), journey (outside travel).
**Before:** Let's delve into the intricate tapestry of the modern data landscape.
**After:** Here is how the data pipeline works.
**Fix:** Substitute the ordinary word. `leverage` is `use`. `utilize` is `use`. `foster` is `encourage`. `myriad` is `many`. `robust` is usually nothing.

### 8. Copula Avoidance
**Tell:** Dodging plain "is" and "are" with serves as, stands as, represents, constitutes, functions as, emerges as.
**Before:** The parser serves as the entry point for all incoming requests.
**After:** The parser is the entry point for all incoming requests.
**Fix:** Use "is." It is not a weak verb. It is the correct one.

### 9. Negative Parallelism
**Tell:** The "not X, but Y" and "not only X but also Y" scaffolds, especially stacked.
**Before:** This isn't just a refactor, it's a rethinking of how the system handles state.
**After:** The refactor changes how the system handles state.
**Fix:** State the positive claim once. The negated half is almost always filler.

### 10. Rule of Three Padding
**Tell:** Triads where the third item adds nothing, repeated across paragraphs until the rhythm becomes audible.
**Before:** The approach is fast, efficient, and performant.
**After:** The approach is fast.
**Fix:** Keep the item that carries information. Vary list lengths across the piece. Two is a fine number. So is four.

### 11. Elegant Variation
**Tell:** Cycling synonyms for one referent to avoid repetition: the company, the firm, the tech giant, the Cupertino-based manufacturer.
**Before:** Apple released the update. The tech giant said the Cupertino-based firm had tested it for months.
**After:** Apple released the update. The company said it had tested the update for months.
**Fix:** Pick one name and repeat it. Repetition reads as clarity, not poverty.

### 12. False Ranges
**Tell:** "From X to Y" where X and Y are not endpoints of any spectrum.
**Before:** The library handles everything from parsing to caching to logging.
**After:** The library handles parsing, caching, and logging.
**Fix:** If the two poles do not define a continuum, it is a list. Write it as one.

### 13. Passive Voice and Orphaned Subjects
**Tell:** Agentless passives that hide who acted, plus subjectless fragments beginning with a participle.
**Before:** It was decided that the endpoint would be deprecated. Having reviewed the logs, the cause was identified.
**After:** The platform team deprecated the endpoint. We reviewed the logs and found the cause.
**Fix:** Name the actor. Passive voice is fine when the actor is genuinely unknown or irrelevant; it is a tell when it is used to avoid committing.

### 14. Hyphenated Modifier Stacks
**Tell:** Pairs of compound adjectives used as decoration: well-crafted, thought-provoking, fast-paced, ever-evolving, cutting-edge, state-of-the-art.
**Before:** A well-crafted, thought-provoking essay on our ever-evolving digital landscape.
**After:** An essay on how moderation policy changed after 2016.
**Fix:** Delete the modifiers and say what the thing is about.

## C. FORMATTING PATTERNS

### 15. Em Dash Overuse
**Tell:** Em dashes used as a general-purpose connector, several per paragraph, where a comma, colon, period, or parentheses would serve.
**Before:** The build failed — a missing dependency — and the fix was simple — pin the version.
**After:** The build failed because of a missing dependency. The fix was simple: pin the version.
**Fix:** Default to cutting. Convert to a period when the halves are independent, a colon when the second half explains the first, a comma for a light aside, parentheses for a true aside. Keep at most one per few hundred words, and only where it earns its place.

### 16. Boldface Decoration
**Tell:** Bold applied to phrases mid-sentence for emphasis rather than to label a term.
**Before:** This is **critically important** and you should **never** skip it.
**After:** Skipping this corrupts the index.
**Fix:** Remove the bold and let the sentence carry the weight. Bold is for labels and defined terms.

### 17. Inline-Header Lists
**Tell:** Bullet lists where each item is a bolded noun phrase, a colon, then a sentence restating the noun.
**Before:** - **Speed:** The system is fast. / - **Reliability:** The system is reliable.
**After:** The system answers in under 40ms and has not dropped a request since March.
**Fix:** If every bullet follows the label-colon-restatement shape, convert the list to prose. Keep the list only when items are genuinely parallel and scannable.

### 18. Title Case Headings
**Tell:** Every Heading Capitalized Like This, in a document whose style is otherwise sentence case.
**Fix:** Match the surrounding convention. Sentence case for prose documents unless the house style says otherwise.

### 19. Emoji and Icon Decoration
**Tell:** Emoji prefixed to headings or bullets in technical or formal writing where the author would not have added them.
**Fix:** Remove them, unless the piece is deliberately informal and already uses them consistently. Do not strip emoji from chat messages, release notes, or personal writing where they fit the voice.

### 20. Typographic Artifacts
**Tell:** Curly quotes, ellipsis characters, and non-breaking spaces in a plain-text or code context that otherwise uses straight ASCII.
**Fix:** Normalize to the document's existing convention. Do not "fix" a document that consistently uses typographic quotes.

## D. VOICE AND STANCE PATTERNS

### 21. Sycophancy
**Tell:** Opening with praise for the question or the reader.
**Before:** Great question! That's a really insightful way to think about it.
**After:** Delete entirely and start with the answer.
**Fix:** Cut the whole clause. Never replace it with a milder compliment.

### 22. Assistant Artifacts in Prose
**Tell:** Conversational scaffolding left in a document: "Let me know if you'd like me to expand," "I hope this helps," "Here's a breakdown of," "Sure! Below is."
**Fix:** Delete. These belong in chat, not in the artifact.

### 23. Cutoff Disclaimers and Speculative Filling
**Tell:** "As of my last update," "I don't have access to real-time data," or confident invention of specifics to patch a gap.
**Fix:** Delete the disclaimer. If a fact is genuinely unknown, say so in the author's voice ("the 2025 figures are not published yet") rather than in the model's.

### 24. Signposting
**Tell:** Announcing structure instead of using it: "In this section, we will explore," "First, let's look at," "Now that we've covered X, let's turn to Y."
**Before:** In this section, we will explore three approaches to caching.
**After:** Three approaches to caching are worth considering.
**Fix:** The heading already signposts. Delete the announcement and start.

### 25. Generic Positive Conclusions
**Tell:** A final paragraph that summarizes nothing and affirms everything: "Overall, X remains an important tool that will continue to shape the field."
**Fix:** End on the last real point, or on a specific consequence. A piece may simply stop. Most conclusions can be deleted with no loss.

### 26. Excessive Hedging
**Tell:** Stacked qualifiers: "may potentially," "could arguably suggest," "it is possible that this might."
**Before:** This could potentially indicate that there may be a memory leak.
**After:** This suggests a memory leak.
**Fix:** Keep one hedge at most. Two hedges in a row cancel into noise.

### 27. Filler Openers
**Tell:** "It's important to note that," "It's worth mentioning," "In today's fast-paced world," "At its core," "When it comes to."
**Before:** It's important to note that the cache expires after an hour.
**After:** The cache expires after an hour.
**Fix:** Delete the opener. The sentence starts at the verb-bearing clause.

### 28. Rhetorical Question Openers
**Tell:** Opening a section by asking the reader a question the text immediately answers: "Ever wondered why builds get slower?"
**Fix:** Replace with the assertion. "Builds get slower as the module graph grows."

### 29. Manufactured Drama
**Tell:** One-sentence paragraphs deployed for punch, especially as a turn: "But there's a catch." "And that changes everything." "The result? A total rewrite."
**Fix:** Fold the sentence into the surrounding paragraph and state the substance. Keep a short paragraph only where the emphasis is genuinely earned and rare.

### 30. Aphorism Formulas
**Tell:** Symmetrical pseudo-wisdom: "X isn't about Y. It's about Z." "The best X is the one you don't need."
**Before:** Good documentation isn't about explaining the code. It's about explaining the decision.
**After:** Good documentation explains why a decision was made, not what the code does.
**Fix:** Convert the epigram into the plain claim. If nothing survives the conversion, it was decoration.

## What NOT to Flag

False positives make this skill worse than useless. Leave these alone:

- **Real em dashes.** One well-placed dash in a long piece is style, not a tell.
- **Genuine triads.** "Life, liberty, and the pursuit of happiness" is a quotation. Established triads and idioms stay.
- **Technical terms that resemble AI vocabulary.** `robust` in statistics, `landscape` in genomics, `ecosystem` in biology, `pivotal` in mechanics, `paradigm` in Kuhn's sense, `delve` in a piece about mining.
- **Deliberate promotional copy.** If the author is writing an ad, promotional language is the assignment.
- **Passive voice with a reason.** Scientific method sections and incident reports use it correctly.
- **Bold in reference docs.** Labels, defined terms, and parameter names are supposed to be bold.
- **Quoted material.** Anything inside quotation marks and attributed stays exactly as written, tells and all.
- **Formal register.** Legal, academic, and regulatory writing is stiff on purpose. Stiff is not the same as machine-made.

When in doubt, do not edit. An unnecessary change costs more credibility than a missed pattern.

## Signs of Human Writing (Preserve These)

If the text already has these, protect them. They are the texture the patterns above destroy:

- Specific, checkable, slightly odd detail. The wrong-sounding number. The brand name.
- Opinions with a stake in them, including ones the editor disagrees with.
- Sentence length that varies without a rhythm.
- Digressions and asides that do not serve the thesis.
- Admitted uncertainty in the author's own voice.
- Humor that would not survive being explained.
- Idiosyncratic punctuation and paragraphing that is consistent with itself.
- Repetition used deliberately for emphasis.

## Invocation Modes

**Rewrite (default).** Return the edited text and nothing else. No preamble, no
summary of changes, no offer to continue.

**Review.** When asked to review, critique, or check rather than rewrite: return
a list of findings as `pattern number, location, quoted span, suggested
replacement`. Change nothing.

**File target.** When given a file path, read it, edit it in place, and report
only which patterns were applied and how many times.

**Score.** When asked how AI-generated something reads: give a 0 to 10 estimate,
list the three strongest tells with quoted evidence, and stop.

## Process and Output

1. Read the whole piece before editing. Determine the register.
2. Pass once for substance patterns (1 to 6), which change what sentences claim.
3. Pass again for language, formatting, and voice (7 to 30).
4. Read the result start to finish. Check that no fact changed, no citation
   moved, and no code was touched.
5. Check your own output against this list. The most common failure is replacing
   one set of tells with another: stripping em dashes and leaving 14 semicolons,
   or cutting hedges and producing 20 flat declaratives of identical length.

Return only the rewritten text unless the mode above says otherwise.

## Reference

The pattern taxonomy draws on Wikipedia's "Signs of AI writing" guide,
maintained by WikiProject AI Cleanup, which catalogues the tells that editors
find in machine-generated article text. That page is available under CC BY-SA
4.0. The packaging conventions follow the portable Agent Skills format so the
file works in any harness that loads Markdown instructions.
