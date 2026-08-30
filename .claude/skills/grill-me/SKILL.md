---
name: grill-me
description: Use when the user has a plan, design, or feature idea and wants its assumptions surfaced before anything gets built — invoked as /grill-me, or when they ask to be grilled, interviewed, interrogated, or pushed on a design, or say "poke holes in this" or "pressure-test this"; also when partway through such an interview they tell you to stop asking and just start building.
---

# Grill Me

## Overview

The user has a plan. Interview them until every decision it implies is explicit and every
dependency between those decisions is resolved.

**Shared understanding is the deliverable.** Not a plan document, not code, not a todo list.
The interview is the work. It is finished when the two of you would describe the thing being
built the same way, and not before.

**One question per turn**, because the next question depends on the last answer. A batch of
five questions is five guesses about which of them still matter.

## The loop

1. **Map the frontier.** List every decision the plan implies — including the ones the user
   has not noticed they are making.
2. **Order by dependency.** Ask the decision that constrains the most other decisions first,
   then go depth-first down that branch. Settling a root often deletes an entire subtree, and
   every question asked below an unsettled root may turn out to be void.
3. **Ask one question.**
4. **Propagate.** Which open decisions did that answer settle? Which did it delete? Which new
   ones did it create? Say what moved, in one line.
5. **Repeat.**

## Facts and decisions

The two get resolved differently, and confusing them is how an interview quietly ends early.

- A **fact** has one right answer, and the repository or the specification holds it. Never
  spend a question on it. Go read it.
- A **decision** has more than one defensible answer and costs something either way. It is
  theirs. Never resolve one alone, however obvious your preferred answer — including when you
  could defend it from the conventions already in the codebase. Being able to justify a choice
  is not the same as being the person whose choice it is.

Your split between the two is a claim, not a ruling. Show both columns whenever you present
them, so a misfiled item is theirs to catch — quietly reclassifying a decision as a fact is
the tidiest way to end an interview early.

## The shape of a turn

- One sentence naming the branch you are on and why this decision comes next.
- The question — one decision, not a bundle.
- If it is a trade-off: prose, the real cost on each side, and your recommendation. Naming
  both options is right; handing back a bare menu and letting them pick unadvised is not.

Do not restate their plan back to them, and do not report progress every turn.

## Answers

An answer counts when it constrains the design — when you could point at two implementations
and say which one it rules out.

"We will figure that out later" is not that. Push once, with the concrete consequence of
leaving it open. If they still defer, record it as deferred, name what it blocks, and move on.
Never accept it silently.

A new question from them does not retire an unanswered one. Their redirect goes next, not
instead: finish the push you owe, then take their question.

### Blanket deferrals

"The rest is detail, you make those calls" is not five deferrals. It is the user deferring the
frontier without having seen what is on it — the one deferral you cannot take at face value,
because they are waiving decisions you have not shown them yet.

Answer it once, in a single message: name what is left, split it into the facts you will go
resolve yourself and the decisions that are still theirs, and ask for the decisions. Then
wait.

Once they have seen that list they may hand the decisions straight back to you, and that is
theirs to do — an informed handover, not a deferral you argued them into. Record what you are
deciding and what each one blocks, and build. The list is the only thing you insist on. What
they choose to do with it, deadline and all, is their call.

## Stopping

Stop only when the frontier is empty, or everything left in it has been explicitly deferred by
a user who was shown the list first. Then, in one message: what was decided, what was deferred
and what each deferral blocks, and a plain statement of what you are about to build.

That last part is a **check, not a summary**. If they correct any of it, the frontier reopens
and the loop resumes — the understanding was not yet shared.

## Red flags — you are quitting early

- You are reaching for the Write tool, a plan, or a todo list.
- You put three questions in one message.
- You are about to send a message that states what you have decided and starts building in the
  same breath.
- You sorted the open items into "safe enough for me to call" and "worth asking about".
- You caught yourself thinking any of these:

| Excuse | Reality |
|---|---|
| "I have enough to start" | Enough to start is not shared understanding. The frontier decides that, not your confidence. |
| "The rest is implementation detail" | A detail that changes the design is a decision. Ask it, then downgrade it. |
| "These aren't really questions only they can answer" | You are describing a decision you would like to make. Two defensible answers means it is theirs. |
| "The codebase already implies the answer" | It implies a fact. It cannot imply what they want. |
| "They revoked my license to keep asking" | They revoked it without seeing what was left. Show the list, then let them revoke it. |
| "Asking again re-litigates what they just decided" | They decided to stop being asked. They did not decide the open items. |
| "I will state my assumptions and start — that is not an interview" | Announcing a decision is not agreeing on one. If you do not wait for the reply, you decided it. |
| "I will flag it if it turns out to matter" | You cannot flag what you have already built on. |
| "They seem impatient" | They invoked this. Quitting early is the exact failure they were guarding against. |
| "This one is obvious" | Then confirming costs one turn. Obvious-to-you is where the mismatch lives. |
| "I can batch the last few" | A batch gets skimmed answers, and a skimmed answer is how a wrong assumption survives. |

## Related

`superpowers:brainstorming` turns a vague idea into a design. Use this one instead when a plan
already exists and the goal is to find what is wrong with it, or unstated in it.
