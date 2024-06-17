import { TelemetryRepository } from '@/domain/repositories/telemetry.interface';
import {
  CrashReportAttributes,
  Telemetry,
  TelemetrySource,
} from '@/domain/telemetry/telemetry.interface';
import { ContextService } from '@/util/context.service';
import { HttpException, Inject, Injectable, Scope } from '@nestjs/common';

@Injectable({ scope: Scope.TRANSIENT })
export class TelemetryUsecases {
  constructor(
    @Inject('TELEMETRY_REPOSITORY')
    private readonly telemetryRepository: TelemetryRepository,
    private readonly contextService: ContextService,
  ) {
    process.on('uncaughtException', async (error: Error) => {
      telemetryRepository.createCrashReport(this.buildCrashReport(error));
    });

    process.on('unhandledRejection', async (error: Error) => {
      telemetryRepository.createCrashReport(this.buildCrashReport(error));
    });
  }

  async createCrashReport(
    error: HttpException | Error,
    source: TelemetrySource,
  ): Promise<void> {
    try {
      const isCollectingTelemetryEnabled = process.env.CORTEX_CRASH_REPORT;
      if (isCollectingTelemetryEnabled === '0') {
        return;
      }
      const crashReport: CrashReportAttributes = this.buildCrashReport(error);

      await this.telemetryRepository.createCrashReport(crashReport, source);
    } catch (e) {
      console.log('Error creating crash report', e);
    }
    return;
  }

  async sendCrashReport(): Promise<void> {
    const crashReport = await this.telemetryRepository.getLastCrashReport();
    if (crashReport && !crashReport.metadata.sentAt) {
      const promises = [
        this.telemetryRepository.sendTelemetryToServer(crashReport),
      ];
      const collectorEndpoint = process.env.CORTEX_EXPORTER_OLTP_ENDPOINT;
      if (collectorEndpoint) {
        promises.push(
          this.telemetryRepository.sendTelemetryToOTelCollector(
            collectorEndpoint,
            crashReport,
          ),
        );
      }
      await Promise.all(promises);
      await this.telemetryRepository.markLastCrashReportAsSent();
    }
    return;
  }

  async readCrashReports(
    callback: (Telemetry: Telemetry) => void,
  ): Promise<void> {
    return this.telemetryRepository.readCrashReports(callback);
  }

  private buildCrashReport(
    error: HttpException | Error,
  ): CrashReportAttributes {
    return {
      message: error.message,
      stack: error.stack,
      payload: {
        modelId: this.contextService.get('modelId') || '',
        endpoint: this.contextService.get('endpoint') || '',
        command: this.contextService.get('command') || '',
      },
    };
  }
}