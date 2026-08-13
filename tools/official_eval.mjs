// Headless harness around the OFFICIAL GIS Cup 2026 evaluator.
//
// The organizers publish their grading code and commit to consistency with it,
// so it is ground truth above our own figure oracle. This runs it end to end --
// dataset-loader -> solution-parser -> submission-validator -> evaluation-engine
// -- against a dataset and a submission, and prints the verdict it would award.
//
// This exists because our internal gates could not, even in principle, catch a
// disagreement with the grader: they all used our own reader and our own
// geometry. Reading the grader's parser already found a defect that would have
// scored every block zero (see FINDINGS). Running the grader closes the rest.
//
//   node tools/official_eval.mjs <evaluator-dir> <dataset.geojson> <submission.txt> [--json out]
import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [evalDir, datasetPath, submissionPath] = process.argv.slice(2);
if (!evalDir || !datasetPath || !submissionPath) {
  console.error("usage: official_eval.mjs <evaluator-dir> <dataset> <submission> [--json out]");
  process.exit(2);
}
const jsonIdx = process.argv.indexOf("--json");
const jsonOut = jsonIdx > 0 ? process.argv[jsonIdx + 1] : null;

const core = (m) => pathToFileURL(resolve(evalDir, "src/core", m)).href;
const { parseBuildingDatasetText } = await import(core("dataset-loader.ts"));
const { parseSolutionText } = await import(core("solution-parser.ts"));
const { validateSubproblemInput } = await import(core("submission-validator.ts"));
const { evaluateValidatedSubproblem } = await import(core("evaluation-engine.ts"));
const { EVALUATOR_VERSION, ARCGIS_VERSION, SPATIAL_TOLERANCE_METERS } =
  await import(core("constants.ts"));

console.error(`[official] evaluator ${EVALUATOR_VERSION}, @arcgis/core ${ARCGIS_VERSION}, `
  + `spatial tolerance ${SPATIAL_TOLERANCE_METERS} m`);

const t0 = Date.now();
const dataset = parseBuildingDatasetText(readFileSync(datasetPath, "utf8"));
console.error(`[official] dataset: ${dataset.buildings.length} buildings `
  + `(${Date.now() - t0} ms)`);

const solution = parseSolutionText(readFileSync(submissionPath, "utf8"));
for (const w of solution.warnings) console.error(`[official] PARSE WARNING ${w.code}: ${w.message}`);

const blocks = [];
let total = 0;
for (const sub of solution.subproblems) {
  const started = Date.now();
  const validated = validateSubproblemInput(sub, dataset);
  for (const w of validated.warnings ?? []) {
    console.error(`[official] block ${sub.index} VALIDATION ${w.code}: ${w.message}`);
  }
  const result = evaluateValidatedSubproblem(dataset, validated, {
    fullDiagnosticCoverage: Boolean(jsonOut),
  });
  const score = result.verifiedServiceScore;
  total += score;
  blocks.push({
    index: sub.index,
    tau: sub.tau,
    k: sub.k,
    claimed: sub.claimedBuildingIds.length,
    verified: score,
    ms: Date.now() - started,
    warnings: (result.warnings ?? []).map((w) => w.code),
    coverage: jsonOut
      ? Object.fromEntries((result.buildingResults ?? []).map((r) => [
          r.buildingId ?? r.building?.id ?? r.visibility?.building?.id,
          {
            visible: r.visibleLengthMeters ?? r.visibility?.visibleLengthMeters,
            perimeter: r.perimeterMeters ?? r.visibility?.perimeterMeters,
            verified: r.verified,
          },
        ]))
      : undefined,
  });
  console.log(`block ${sub.index}  tau=${sub.tau} k=${sub.k}  claimed=${sub.claimedBuildingIds.length}`
    + `  VERIFIED=${score}  (${Date.now() - started} ms)`
    + ((result.warnings ?? []).length ? `  warnings=${result.warnings.length}` : ""));
}

console.log(`\nofficial total verified: ${total}   [${Date.now() - t0} ms]`);
if (jsonOut) {
  writeFileSync(jsonOut, JSON.stringify({
    evaluatorVersion: EVALUATOR_VERSION, arcgisVersion: ARCGIS_VERSION, blocks,
  }));
  console.error(`[official] per-building coverage -> ${jsonOut}`);
}
