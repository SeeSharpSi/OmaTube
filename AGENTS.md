# Design and Implementation Guidance

Design is how a thing explains itself, performs its work, resists waste, and respects the people using it. Treat every interface, word, delay, dependency, and failure mode as part of the design.

Optimize for user capability, comprehension, trust, performance, and repairability. Do not optimize for engagement, surveillance, novelty, investor narratives, or organizational convenience at the user's expense.

These principles guide judgment; they do not replace it. Context and observed behavior outrank doctrine.

## Decision Order

When values compete, generally prefer:

1. User agency and preservation of user work.
2. Clear state, consequences, and recovery.
3. Reliable and fast completion of the user's task.
4. Legible, local, and maintainable implementation.
5. Visual coherence and consistency.
6. Novelty, fashion, and institutional convention.

## Before Changing Anything

- Identify the people, task, and concrete problem the change serves.
- Inspect current behavior and constraints before proposing a replacement.
- Assume unfamiliar complexity may encode a requirement or a past failure until evidence shows otherwise.
- Define success in terms of capability, comprehension, speed, safety, or coherence, not output volume.
- Prefer the smallest deliberate change that solves the demonstrated problem.
- Make changes reversible when practical.

## Spend Complexity Deliberately

Complexity is finite. Spend it only where it creates meaningful capability or makes work safer, clearer, or faster.

- Account for comprehension, maintenance, support, change, and failure costs.
- Require every feature, abstraction, service, state container, dependency, and process layer to earn its place.
- Prefer direct, ordinary mechanisms when they are sufficient.
- Remove waste, not useful controls, information, or capability.
- Do not hide necessary complexity behind fashionable abstractions or sparse aesthetics.

## Say No with Precision

- Every feature spends user attention, implementation effort, support capacity, and future flexibility.
- Decline features or scope that weaken the product's purpose or add cost without meaningful capability.
- Finish and improve valuable existing work before expanding scope.
- Treat deliberate omission as design judgment, not lack of imagination.

## Make Interfaces Explicit

- Show current state, mode, progress, ownership, available actions, consequences, and source of truth.
- Prefer labels, named actions, visible controls, and clear status messages over implication, gesture, unlabeled icons, or clever defaults.
- Keep actions close to their consequences.
- Make loading and waiting visible when they affect the task.
- Explain failures in plain language: what happened, what was preserved, and what the user can do next.
- Give users control where choices matter. Do not use dark patterns, manipulation, or attention extraction.
- Treat users as capable adults. Offer help without creating dependency or obstructing experienced users.

## Preserve Work and Trust

- Never discard user work silently.
- Design for interruption, reloads, weak networks, partial requests, stale state, and ordinary mistakes.
- Provide clear recovery paths and make destructive consequences explicit before commitment.
- Prefer durable state and honest feedback over optimistic appearances that can mislead users.

## Use Density with Purpose

Clarity is not the same as emptiness. Work often requires comparison, context, and control.

- Use hierarchy, alignment, typography, grouping, contrast, and spacing to make dense information scannable.
- Keep useful controls and context visible when they support confident action.
- Do not remove capability merely to make an interface look clean.
- Judge density by confusion, hesitation, and wasted motion, not by element count.

## Let Context Outrank Consistency

- Reuse patterns when reuse reduces learning cost and improves prediction.
- Break consistency when the task, medium, consequence, or user need demands a clearer form.
- Do not preserve a convention solely because it already exists.
- Keep basic paths approachable without withholding advanced capability from people who need it.

## Prefer the Grain of the Web

- Use links, forms, URLs, HTML, CSS, HTTP, browser history, and other platform capabilities before building private substitutes.
- Preserve expected browser behavior, direct navigation, and inspectable output.
- Start from a clear document or request-response model; enhance it when scripting produces a demonstrated benefit.
- Favor durable interfaces, repairable systems, and behavior that can be traced from input to output.

## Keep Code Local and Legible

- Keep behavior near the declaration, component, route, or interface it affects.
- Keep documentation near the behavior it explains.
- Avoid hidden dependencies, implicit global effects, and spooky action at a distance.
- Prefer code that reveals control flow and state transitions over code that merely reduces line count.
- Introduce abstractions only when they make repeated work clearer, safer, faster, or more coherent.
- Make boundaries and sources of truth explicit.

## Treat Performance as Design

- Make common paths fast and measure actual latency where performance matters.
- Include network, rendering, interaction, and recovery time in design decisions.
- Test under realistic constraints, including weak networks and older devices when relevant.
- Do not postpone responsiveness and reliability as polish; both shape comprehension and trust.

## Make Form and Language Functional

- Use typography, layout, graphics, motion, and contrast to communicate structure and operational meaning.
- Add visual distinction when it improves perception; do not add decoration that competes with the task.
- Name things honestly and use the user's vocabulary rather than internal jargon.
- Write controls as actions and confirmations as specific outcomes.
- Avoid hype, euphemism, and vague reassurance.
- Use more words when they remove ambiguity and fewer when they remove noise.

## Resist Fashion

- Do not adopt visual patterns, animation, architecture, AI features, or governance layers merely because they are current or prestigious.
- Require novelty to improve task performance, perception, speed, trust, safety, or maintainability.
- Do not imitate large organizations whose constraints do not apply here.
- Define who and what the product serves. Deliberately decline workflows that would erase its focus, even when that disappoints some potential users.

## Validate Against Reality

- Use available user research, issue reports, support requests, logs, and failure paths; look for hesitation, mistakes, workarounds, and confusion.
- Measure latency, reliability, and implementation cost rather than relying on taste.
- Test assumptions with working software or the smallest useful prototype.
- Change course when evidence contradicts these principles or the team's preferences.

## Completion Standard

Before considering work complete, verify that:

- The change solves a stated human or system problem.
- Important state and consequences are visible.
- Failures preserve work and offer recovery.
- Added complexity has a clear benefit.
- Behavior remains inspectable and maintainable.
- Common paths remain fast and understandable.
- Words, controls, and visual hierarchy communicate honestly.
- Evidence, not fashion or preference, supports the result.
