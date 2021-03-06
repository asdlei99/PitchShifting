// wavread.cpp: 定义控制台应用程序的入口点
#include "stdafx.h"
#include <iostream>
#include "fftw3.h"
#include <math.h>
#include <vector>
#include "AudioFile.cpp"
//#include "AudioFile.h"
#define PI 3.1415926535897

using namespace std;

/*
Library used in this project:
	1. FFTW: FFT compute
	2. AudioFile: wav file read and write

TODO：
1. C++ wraper for FFTW's FFT ,pitch shifting effect.
2. suport x86/x64 platform (x86:done  x64:todo)

已知BUG：
1. 所换算出来的值比matlab中的值(浮点)少一半，不知为何，需要重新校验matlab中的解码方式，或者
自己编写的解码方式，就目前来说影响不大。// 无法复现

*/
int main(void)
{
	// 在SDK版本不一致时需要更新解决方案(清理+重定向项目SDK) 
	string file_name = "audioCut_2.wav";
	AudioFile<double> af;
	cout << "ready to read the wav file\n";
	af.load(file_name);
	cout << "Read it sucessful\n";
	af.printSummary();
	vector<double> data;		// 即将用于测试的数据, 
	vector<double> data_out;
	if (af.getNumChannels() > 1) {
		for (size_t n = 0; n != af.getNumSamplesPerChannel(); ++n) {		// stereo -> mono
			data.push_back((af.samples.at(0)[n] + af.samples.at(1)[n]) / 2.0);
		}
	}
	else {
		data.assign(af.samples.at(0).begin(), af.samples.at(0).end());
	}
	
	//for (size_t tt = 0; tt < 10; ++tt) {
		//cout << data.at(tt) << endl;
	//}
	//wavfile wav;  // 不用自己编写的了  稍后将会整合
	//wav.read(file_name);
	//wav.show();

	const int win_len = 256;															//win_len ,fft number
	fftw_complex *fft_in, *fft_out,*ifft_in,*ifft_out;
	fftw_plan fft,ifft;
	fft_in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*win_len);
	fft_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*win_len);
	ifft_in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*win_len);
	ifft_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*win_len);
	fft = fftw_plan_dft_1d(win_len, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);		// FFT
	ifft = fftw_plan_dft_1d(win_len, ifft_in, ifft_out, FFTW_BACKWARD, FFTW_ESTIMATE);	// IFFT


	const unsigned int ana_len = 64;					// analysis length
	const unsigned int syn_len = 90;					// synthesis length
	unsigned int ana_count = ( data.size() - (win_len-ana_len) ) / ana_len;	// 有修改
	// fft 相关
	double real[win_len];			// 实部
	double imag[win_len];			// 虚部
	double mag[win_len];			// 幅值
	double angle[win_len] = {0};	// 相角
	double angle_pre[win_len];		// 前一帧相角
	bool is_firstTime = true;
	double angle_syn[win_len];		// 同步相位

	double omega[win_len];
	for (int n = 0; n < win_len; n++) {
		omega[n] = 2 * PI*double(n) / double(win_len);
	}

	double hanning_win[win_len];	// hanning window
	for (int n = 0; n < win_len; n++) {
		hanning_win[n] = 0.5*(1 - cos(2 * PI* n / win_len));
	}

	double overlap_buff[256] = {0};
	//ana_count = 44100*1/ana_len;						//5000-10 seconds/ 29400-40s  test 

	////////ofstream audio_out;									// 输出值txt进行查看 test
	////////audio_out.open("audio6.txt", ios::out | ios::ate);	//  最终输出的音频 test

	//vector<double> original_input;
	vector<double> syn_angle;

	// 开始处理
	//for (int i=0,offset=0; i < ana_count; i++,offset++) {			// 分帧 以ana_len 为间隔向前移动，取出win_len点数据

	for (int i = 0, offset = 0; i < ana_count ; i++, offset++) {			// 分帧 以ana_len 为间隔向前移动，取出win_len点数据

		for (int n = 0; n < win_len; n++) {
			//double input_temp = wav.data[offset*ana_len +n];
			double input_temp = data.at(offset*ana_len +n);
			fft_in[n][0] = input_temp * hanning_win[n];				// 加窗
			fft_in[n][1] = 0;
			//original_input.push_back(input_temp);					// test 记录即将进行处理的原始数据
		}

		fftw_execute(fft);											// 对输入信号使用fft
		for (int n = 0; n < win_len; n++) {
			real[n] = fft_out[n][0];								// 保存实部
			imag[n] = fft_out[n][1];								// 保存虚部
			angle_pre[n] = angle[n];
			// Notice the different between real() and abs() func
			mag[n] = sqrt(real[n] * real[n] + imag[n] * imag[n]);
			// Notice：angle(x+yj) = actan2(y,x);
			angle[n] = atan2(imag[n],real[n]);

			// phase unwrap

			double delta = (angle[n] - angle_pre[n]) - omega[n] * ana_len;	// phi 数组！
			//cout << delta << endl;// test
			double delta_phi = delta - round( delta / (2 * PI) ) * (2 * PI);// phi 
			double y_unwrap = delta_phi/ana_len + omega[n];					// w
			if (is_firstTime) {
				angle_syn[n] = angle[n];
			}
			else {
				angle_syn[n] = angle_syn[n] + y_unwrap * syn_len;			// phi,shifted angle 
			}
			syn_angle.push_back(angle_syn[n]);								// 记录同步相位 test 

			// prepare for ifft
			ifft_in[n][0] = mag[n] * cos(angle_syn[n]);						// 还原实部
			ifft_in[n][1] = mag[n] * sin(angle_syn[n]);						// 还原虚部
		}
		is_firstTime = false;

		// Notice:fftw库中 ifft输出的值没有进行归一化，需要除以ifft_points 才与matlab中的结果相等
		fftw_execute(ifft);

		// 使用ifft_out中的实数部分进行synthesis and overlap add
		double toSOA[win_len];
		double overlap[win_len - syn_len];
		for (int n = 0; n < win_len; n++) {
			toSOA[n]=hanning_win[n]*ifft_out[n][0] / double(win_len);		// 归一化,加窗
			if (n < win_len - syn_len) {
				overlap[n] = overlap_buff[syn_len + n] + toSOA[n];			// overlap add
				overlap_buff[n] = overlap[n];
			}
			else
			{
				overlap_buff[n] = toSOA[n];
			}
		}
		// output is in overlap_buff[0:syn_len]

		for (int output_n = 0; output_n < syn_len; output_n++) {			// output the phase shifting signal ,test
			//audio_out << overlap_buff[output_n] << endl;
			data_out.push_back( overlap_buff[output_n] );		// 输出
		}
		
	}

	vector<vector<double>> buff(2);
	cout << buff.size() << endl;
	buff[0].assign(data_out.begin(),data_out.end());
	buff[1].assign(data_out.begin(),data_out.end());
	af.setAudioBuffer(buff);
	af.save("pitch_test4.wav");
	cout << " pitch test end";

	// 释放资源
	fftw_free(fft_in);
	fftw_free(fft_out);
	fftw_free(ifft_in);
	fftw_free(ifft_out);
	fftw_destroy_plan(fft);
	fftw_destroy_plan(ifft);
	cout << "end here"<<endl;
	system("pause");
	return 0;
}
