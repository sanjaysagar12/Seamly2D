function escapeXml(value: string): string {
  return value
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

export interface SmisOptions {
  unit?: string;
  pm_system?: string;
}

/**
 * Builds an individual/single-size .smis measurement file from a flat {name: value} map, matching
 * the format `measurements.load`/`measurements.sync` expect (see src/app/share/samples/measurements/individual/*.smis).
 */
export function buildSmisXml(measurements: Record<string, number>, options: SmisOptions = {}): string {
  const unit = options.unit ?? 'cm';
  const pmSystem = options.pm_system ?? '998';
  const entries = Object.entries(measurements);
  if (entries.length === 0) {
    throw new Error('"measurements" must contain at least one name/value pair.');
  }
  const body = entries
    .map(([name, value]) => {
      if (typeof value !== 'number' || !Number.isFinite(value)) {
        throw new Error(`Measurement "${name}" must have a finite numeric value, got: ${JSON.stringify(value)}`);
      }
      return `        <m name="${escapeXml(name)}" value="${value}"/>`;
    })
    .join('\n');

  return `<?xml version="1.0" encoding="UTF-8"?>
<smis>
    <version>0.3.4</version>
    <read-only>false</read-only>
    <notes/>
    <unit>${escapeXml(unit)}</unit>
    <pm_system>${escapeXml(pmSystem)}</pm_system>
    <personal><family-name/><given-name/><birth-date>1800-01-01</birth-date><gender>unknown</gender><email/></personal>
    <body-measurements>
${body}
    </body-measurements>
</smis>
`;
}
