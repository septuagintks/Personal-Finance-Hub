<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import Decimal from 'decimal.js';
import { BarChart, LineChart } from 'echarts/charts';
import { GridComponent, LegendComponent, TooltipComponent } from 'echarts/components';
import { init, use, type ECharts } from 'echarts/core';
import { CanvasRenderer } from 'echarts/renderers';

export interface ReportChartSeries {
  name: string;
  values: string[];
  color: string;
}

const props = defineProps<{
  kind: 'line' | 'bar';
  labels: string[];
  series: ReportChartSeries[];
  accessibleLabel: string;
}>();

use([BarChart, LineChart, GridComponent, LegendComponent, TooltipComponent, CanvasRenderer]);

const element = ref<HTMLDivElement | null>(null);
let chart: ECharts | null = null;
let observer: ResizeObserver | null = null;

function chartValue(value: string): number {
  try {
    const number = new Decimal(value).toNumber();
    return Number.isFinite(number) ? number : 0;
  } catch {
    return 0;
  }
}

function tooltip(parameters: unknown): string {
  const entries = Array.isArray(parameters) ? parameters : [parameters];
  const first = entries[0] as { dataIndex?: number } | undefined;
  const dataIndex = first?.dataIndex ?? 0;
  const lines = [props.labels[dataIndex] ?? ''];
  for (const entry of entries) {
    const item = entry as { seriesIndex?: number; marker?: string };
    const seriesIndex = item.seriesIndex ?? 0;
    const source = props.series[seriesIndex];
    if (!source) continue;
    lines.push(`${item.marker ?? ''}${source.name}: ${source.values[dataIndex] ?? '0'}`);
  }
  return lines.join('\n');
}

function render(): void {
  if (!chart) return;
  chart.setOption(
    {
      animation: !window.matchMedia('(prefers-reduced-motion: reduce)').matches,
      aria: { enabled: true, description: props.accessibleLabel },
      color: props.series.map(({ color }) => color),
      grid: { left: 14, right: 14, top: 34, bottom: 24, containLabel: true },
      legend: { top: 0, textStyle: { color: '#53625d', fontSize: 11 } },
      tooltip: {
        trigger: 'axis',
        renderMode: 'richText',
        formatter: tooltip,
      },
      xAxis: {
        type: 'category',
        data: props.labels,
        axisLine: { lineStyle: { color: '#cbd3cf' } },
        axisLabel: { color: '#65736f', hideOverlap: true },
        axisTick: { show: false },
      },
      yAxis: {
        type: 'value',
        axisLine: { show: false },
        axisTick: { show: false },
        axisLabel: { color: '#65736f' },
        splitLine: { lineStyle: { color: '#e4e9e6' } },
      },
      series: props.series.map((item) => ({
        name: item.name,
        type: props.kind,
        data: item.values.map(chartValue),
        smooth: props.kind === 'line',
        symbolSize: 7,
        barMaxWidth: 36,
        lineStyle: { width: 2 },
        emphasis: { focus: 'series' },
      })),
    },
    true,
  );
}

onMounted(() => {
  if (!element.value) return;
  chart = init(element.value, undefined, { renderer: 'canvas' });
  render();
  if (typeof ResizeObserver !== 'undefined') {
    observer = new ResizeObserver(() => chart?.resize());
    observer.observe(element.value);
  } else {
    window.addEventListener('resize', resize);
  }
});

function resize(): void {
  chart?.resize();
}

watch(() => [props.kind, props.labels, props.series, props.accessibleLabel], render, {
  deep: true,
});

onBeforeUnmount(() => {
  observer?.disconnect();
  window.removeEventListener('resize', resize);
  chart?.dispose();
  chart = null;
});
</script>

<template>
  <div ref="element" class="report-chart" role="img" :aria-label="accessibleLabel"></div>
</template>
