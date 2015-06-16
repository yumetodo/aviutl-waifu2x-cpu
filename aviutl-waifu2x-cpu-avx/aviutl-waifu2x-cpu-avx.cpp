/* waifu2x-cpu Ver.1.3.1 by YSR */

/* �v���v���Z�b�T */
#pragma warning( disable: 4018)
//C�W�����C�u����
#include <cstdint>
// STL
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
// Windows�ˑ�
#include <tchar.h>
#include <windows.h>
// SIMD�֌W
#include "simd.h"
// AviUtl�֌W
#include "filter.h"

#define kSoftName "waifu2x-cpu[AVX]"

/* using�錾 */
using std::stod;
using std::stoi;
using std::string;

/* Typedef�錾 */
typedef uint32_t Int;

/* �萔�錾 */
// UI�ɂ�����ݒ�
//�g���b�N�o�[(���E���O�E�����l�E�����l�E����l��ݒ肷��)
const int kTracks = 5;
TCHAR *track_name[] = {"thread", "noise", "scale", "block_x", "block_y"};
int   track_default[] = {1, 0, 0, 32, 32};
int   track_s[] = {1, 0, 0, 32, 32};
int   track_e[] = {32, 2, 1, 512, 512};
enum kTrackBar { kTrackThread, kTrackNoise, kTrackScale, kTrackBlockX, kTrackBlockY };
enum kModelKind { kModelDenoise1, kModelDenoise2, kModelScale2x, kModels };
//�`�F�b�N�{�b�N�X(���E���O�E�����l��ݒ肷��)
const int kChecks = 1;
TCHAR *check_name[] = {"use blocking"};
int	  check_default[] = {0};
// �\�t�g�E�F�A�ɂ�����ݒ�
const auto kSteps = 7;		//�X�e�b�v��
const auto kMaxInput = 128;	//���͕��ʂ̍ő吔
const auto kMaxOutput = 128;	//�o�͕��ʂ̍ő吔
const auto kWidthSize = 3;		//��ݍ��݂���d�݂̉��T�C�Y
const auto kHeightSize = 3;		//��ݍ��݂���d�݂̏c�T�C�Y
const auto kFilterSize = kWidthSize * kHeightSize;		//��ݍ��݂���d�݂̑S�̃T�C�Y
const auto SIMD = sizeof(PackedFloat) / sizeof(float);	//SIMD�ɂ����鏈����
const PackedFloat kZeroSIMD = PackedSetZero();			//��r�p�̒l
const PackedFloat kConstSIMD = PackedSet1(0.1);			//��������0.1���|���邽�߂̒l

/* �N���X�E�\���̒�` */
// �t�B���^DLL�p�\����
FILTER_DLL filter = {
	FILTER_FLAG_EX_INFORMATION, 0, 0, kSoftName,
	kTracks, track_name, track_default, track_s, track_e,
	kChecks, check_name, check_default,
	func_proc, func_init, NULL, NULL, NULL,
	NULL, NULL,
	NULL, NULL,
	"waifu2x-cpu version 1.3.1 by YSR",
	NULL, NULL,
};
// 1�X�e�b�v�ɂ�����f�[�^
struct Step{
	Int output_plane_size;
	Int input_plane_size;
	PackedFloat weight_simd[kMaxOutput][kMaxInput][kFilterSize];
	PackedFloat bias[kMaxOutput];
};
// 1���f���ɂ�����f�[�^
struct Model {
	Step step[kSteps];
	// �������֐�
	void Init(const string filename) {
		// �t�@�C�����J���Ȃ��ƃA�E�g
		std::ifstream fin(filename, std::ios_base::in | std::ios_base::binary);
		if(fin.fail()) throw filename;
		// �ǂݍ��݃��[�v
		for(auto s = 0; s < kSteps; ++s) {
			Step *now_step = &step[s];	//���݂�Step���w���|�C���^
			// input�����output
			fin.read(reinterpret_cast<char*>(&now_step->input_plane_size), sizeof(Int));
			fin.read(reinterpret_cast<char*>(&now_step->output_plane_size), sizeof(Int));
			// weight
			for(auto o = 0; o < now_step->output_plane_size; ++o) {
				for(auto i = 0; i < now_step->input_plane_size; ++i) {
					PackedFloat *now_weight_simd = now_step->weight_simd[o][i];
					for(auto k = 0; k < kFilterSize; ++k) {
						float temp;
						fin.read(reinterpret_cast<char*>(&temp), sizeof(float));
						now_weight_simd[k] = PackedSet1(temp);
					}
				}
			}
			// bias
			for(auto o = 0; o < now_step->output_plane_size; ++o) {
				float temp;
				fin.read(reinterpret_cast<char*>(&temp), sizeof(float));
				now_step->bias[o] = PackedSet1(temp);
			}
		}
	}
};

/* �v���g�^�C�v�錾 */
// StretchNN(�t�B���^PROC�p�\���̂ւ̃|�C���^)
void StretchNN(FILTER_PROC_INFO*);
// SetFilter(�t�B���^PROC�p�\���̂ւ̃|�C���^, ��������ۂɎg�����f���f�[�^�̔ԍ�, �X���b�h��, �������̃u���b�N�T�C�Y<, �VY)
void SetFilter(FILTER_PROC_INFO*, const int, const int, const int, const int);

/* �O���[�o���ϐ��錾 */
/* ����Ȃ��Ƃ͂������Ȃ��������I
* �ł��A1�t���[������1�񃂃f���f�[�^��ǂݏo���Ȃ��
* �n���������Ƃ͂������Ȃ��������I
* �N�����ɂ����Ə�肢�Ώ��@�������Ă���c�c
*/
Model g_model_data[kModels];

/* AviUtl����Ăяo�����߂̊֐� */
EXTERN_C FILTER_DLL __declspec(dllexport) * __stdcall GetFilterTable(void) {
	return &filter;
}

/* �������֐� */
BOOL func_init(FILTER *fp) {
	try {
		g_model_data[kModelDenoise1].Init(".\\plugins\\models\\noise1_model.dat");
		g_model_data[kModelDenoise2].Init(".\\plugins\\models\\noise2_model.dat");
		g_model_data[kModelScale2x].Init(".\\plugins\\models\\scale2.0x_model.dat");
	}
	catch(string err_msg_) {
		string err_msg = err_msg_ + "��ǂݍ��ލۂɃG���[���������܂����B";
		MessageBox(NULL, err_msg_.c_str(), kSoftName, MB_OK);
		return FALSE;
	}
	return TRUE;
}

/* �����֐� */
BOOL func_proc(FILTER *fp, FILTER_PROC_INFO *fpip) {
	try {
		// �v�Z���邷��ꍇ�Ƃ��Ȃ��ꍇ�ƂŁA�^�C�g���o�[�̕\����ύX����
		if(!fp->exfunc->is_saving(fpip->editp)) {
			if((fp->track[kTrackNoise] == 0) && (fp->track[kTrackScale] == 0)) {
				// �v�Z���Ȃ��ꍇ
				SetWindowText(fp->hwnd, _T(kSoftName));
				return TRUE;
			} else {
				// �v�Z����ꍇ
				SetWindowText(fp->hwnd, _T("waifu2x-cpu(������...)"));
			}
		}

		// �v�Z����ꍇ�A�ݒ�ɉ����ă��f���f�[�^��I�����A�t�B���^�������s��
		auto start = std::chrono::system_clock::now();
		//�m�C�Y��������ꍇ�́A��Ɋg�傷��ꍇ�ł���ɏ�������
		if(fp->track[kTrackNoise] > 0) {
			SetFilter(fpip, fp->track[kTrackNoise] - 1, fp->track[kTrackThread], fp->check[0] * fp->track[kTrackBlockX], fp->check[0] * fp->track[kTrackBlockY]);
		}
		//�g�傷��ꍇ�́A�܂��ŋߖT�@�Ŋg�債�Ă���t�B���^�������s��
		if(fp->track[kTrackScale] > 0) {
			StretchNN(fpip);
			SetFilter(fpip, kModelScale2x, fp->track[kTrackThread], fp->check[0] * fp->track[kTrackBlockX], fp->check[0] * fp->track[kTrackBlockY]);
		}

		// ���Z���Ԃ��^�C�g���o�[�ɕ\������
		if(!fp->exfunc->is_saving(fpip->editp)) {
			auto end = std::chrono::system_clock::now();
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
			std::stringstream title_bar;
			title_bar << "waifu2x-cpu(" << ms << "ms)";
			SetWindowText(fp->hwnd, _T(title_bar.str().c_str()));
		}
	}
	catch(...) {
		if(!fp->exfunc->is_saving(fpip->editp)) {
			SetWindowText(fp->hwnd, _T(kSoftName));
			MessageBox(NULL, "���������m�ۂł��܂���ł����B", kSoftName, MB_OK);
		}
		return FALSE;
	}
	return TRUE;
}

/* �ŋߖT�@�ɂ��g����s�� */
void StretchNN(FILTER_PROC_INFO *fpip) {
	// �g��Ɏg�p���鏈���T�C�Y�����肷��
	// (�g���ɉ摜�̈悩��͂ݏo�Ȃ��悤�ɂ���)
	auto scale_size_x = fpip->w;
	if(scale_size_x * 2 > fpip->max_w) scale_size_x = fpip->max_w / 2;
	auto scale_size_y = fpip->h;
	if(scale_size_y * 2 > fpip->max_h) scale_size_y = fpip->max_h / 2;
	// �ŋߖT�@�ŁAfpip->ycp_edit����fpip->ycp_temp�Ɍ������Ċg�傷��
	// (�P��2�{�ɂ��Ă��邾���Ȃ̂Ŋy�ɋL�q�ł���)
	for(auto y = 0; y < scale_size_y; y++) {
		auto ycp_from = fpip->ycp_edit + y     * fpip->max_w;
		auto ycp_to = fpip->ycp_temp + y * 2 * fpip->max_w;
		for(auto x = 0; x < scale_size_x; x++) {
			for(auto k = 0; k < 2; ++k) {
				ycp_to->y = ycp_from->y;
				ycp_to->cb = ycp_from->cb;
				ycp_to->cr = ycp_from->cr;
				ycp_to[fpip->max_w].y = ycp_from->y;
				ycp_to[fpip->max_w].cb = ycp_from->cb;
				ycp_to[fpip->max_w].cr = ycp_from->cr;
				++ycp_to;
			}
			++ycp_from;
		}
	}
	// ���݂̉�ʃT�C�Y��ύX����
	fpip->w = scale_size_x * 2;
	fpip->h = scale_size_y * 2;
	// �Ō�Ƀ|�C���^�����ւ���
	auto ycp = fpip->ycp_edit;
	fpip->ycp_edit = fpip->ycp_temp;
	fpip->ycp_temp = ycp;
}

/* �t�B���^�������s�� */
/* mode_         �c�c kModelKind�ɑΉ����Ă���B0�`2���f�m�C�Y���x��1�E�f�m�C�Y���x��2�E�g��
* thread_       �c�c ��������X���b�h��
* block_size_x_ �c�c ��������ۂ̃u���b�N�T�C�YX�B0���Ɖ����Ɠ����ɂȂ�
* block_size_y_ �c�c ��������ۂ̃u���b�N�T�C�YY�B0���Əc���Ɠ����ɂȂ�
*/
void SetFilter(FILTER_PROC_INFO *fpip, const int mode_, const int thread_, const int block_size_x_, const int block_size_y_) {
	/* �u���b�N��������ۂ̃u���b�N�T�C�Y�����肷��
	* 0���Ɖ��E�c���Ɠ����ɂȂ�̂ŁA�������Ȃ��Ă����������𓥂܂��邱�ƂɂȂ�
	*/
	auto block_size_x = block_size_x_;
	if(block_size_x == 0) block_size_x = fpip->w;
	auto block_size_y = block_size_y_;
	if(block_size_y == 0) block_size_y = fpip->h;

	/* �c���́A���E�E�����̃p�f�B���O�T�C�Y�����肷��
	* �E�X�e�b�v����X�Ƃ����ہA�p�f�B���O�͉摜�̏㉺���E��X�ȏ㖳����΂Ȃ�Ȃ�
	* �E�u���b�b�L���O����ꍇ�A�摜���u���b�N�ɕ���������A�p�f�B���O���݂̃f�[�^����������K�v������
	* �ESIMD�����̊ϓ_����A�p�f�B���O��̉����͏�����(SSE�n����128/32��4�AAVX�E�y����8)�Ŋ���؂��Ɣ�������
	*/
	// �E�����̃p�f�B���O�T�C�Y(�������K�v)
	// �Ⴆ�Ώ�����4��3�h�b�g�]���1�h�b�g�t�������A������8��5�h�b�g�]���3�h�b�g�t������
	auto padding_x = kSteps;
	auto rightest_block_size = fpip->w % block_size_x;
	if(rightest_block_size == 0) rightest_block_size = block_size_x;
	auto surplus = (kSteps + rightest_block_size + kSteps) % SIMD;
	if(surplus != 0) padding_x += (SIMD - surplus);
	//�������̃p�f�B���O�T�C�Y(���̂܂܂�OK)
	auto padding_y = kSteps;

	/* ���肵���p�f�B���O�T�C�Y�ɏ]���p�f�B���O���� */
	// Y������[0,1]�ɐ��K������
	auto padded_picture_x = kSteps + fpip->w + padding_x;
	auto padded_picture_y = kSteps + fpip->h + padding_y;
	float *padded_picture = (float*)_mm_malloc(sizeof(float) * padded_picture_y * padded_picture_x, alignment_size);
	if(padded_picture == NULL) throw 0;
	for(auto y = 0; y < fpip->h; y++) {
		auto ycp = fpip->ycp_edit + y * fpip->max_w;
		for(auto x = 0; x < fpip->w; x++) {
			float normalized_y = 1.0f * ycp->y / 4096;
			if(normalized_y < 0.0f) normalized_y = 0.0f;
			if(normalized_y > 1.0f) normalized_y = 1.0f;
			padded_picture[(y + kSteps) * padded_picture_x + x + kSteps] = normalized_y;
			ycp++;
		}
	}
	// �ӂ̕������g������(�r���A�e���|�����ȕϐ������ނ��Ƃō�������}����)
	//����
	auto temp = padded_picture[kSteps * padded_picture_x + kSteps];
	for(auto y = 0; y < kSteps; ++y) {
		for(auto x = 0; x < kSteps; ++x) {
			padded_picture[y * padded_picture_x + x] = temp;
		}
	}
	//�E��
	temp = padded_picture[kSteps * padded_picture_x + fpip->w + kSteps - 1];
	for(auto y = 0; y < kSteps; ++y) {
		for(auto x = fpip->w + kSteps; x < padded_picture_x; ++x) {
			padded_picture[y * padded_picture_x + x] = temp;
		}
	}
	//�E��
	temp = padded_picture[(fpip->h + kSteps - 1) * padded_picture_x + fpip->w + kSteps - 1];
	for(auto y = fpip->h + kSteps; y < padded_picture_y; ++y) {
		for(auto x = fpip->w + kSteps; x < padded_picture_x; ++x) {
			padded_picture[y * padded_picture_x + x] = temp;
		}
	}
	//����
	temp = padded_picture[(fpip->h + kSteps - 1) * padded_picture_x + kSteps];
	for(auto y = fpip->h + kSteps; y < padded_picture_y; ++y) {
		for(auto x = 0; x < kSteps; ++x) {
			padded_picture[y * padded_picture_x + x] = temp;
		}
	}
	//��
	auto *temp_p = &padded_picture[kSteps * padded_picture_x];
	for(auto y = 0; y < kSteps; ++y) {
		for(auto x = kSteps; x < fpip->w + kSteps; ++x) {
			padded_picture[y * padded_picture_x + x] = temp_p[x];
		}
	}
	//�E
	for(auto y = kSteps; y < fpip->h + kSteps; ++y) {
		temp = padded_picture[y * padded_picture_x + fpip->w + kSteps - 1];
		for(auto x = fpip->w + kSteps; x < padded_picture_x; ++x) {
			padded_picture[y * padded_picture_x + x] = temp;
		}
	}
	//��
	temp_p = &padded_picture[(fpip->h + kSteps - 1) * padded_picture_x];
	for(auto y = fpip->h + kSteps; y < padded_picture_y; ++y) {
		for(auto x = kSteps; x < fpip->w + kSteps; ++x) {
			padded_picture[y * padded_picture_x + x] = temp_p[x];
		}
	}
	//��
	for(auto y = kSteps; y < fpip->h + kSteps; ++y) {
		temp = padded_picture[y * padded_picture_x + kSteps];
		for(auto x = 0; x < kSteps; ++x) {
			padded_picture[y * padded_picture_x + x] = temp;
		}
	}

	/* �p�f�B���O���ʂ��u���b�N�ɕ������ď������� */
	// ���������v�Z
	auto block_num_x = fpip->w / block_size_x;
	if(fpip->w % block_size_x != 0) ++block_num_x;
	auto block_num_y = fpip->h / block_size_y;
	if(fpip->h % block_size_y != 0) ++block_num_y;
	// �c�����ɂ�������̓T�C�Y(input_size_y)�Əo�̓T�C�Y(output_size_y)�����肷��
	auto input_size_y = block_size_y + kSteps * 2;
	auto output_size_y = block_size_y;
	// ���[�v����
	for(auto block_pos_y = 0; block_pos_y < block_num_y; ++block_pos_y) {
		// ��ԉ��̃u���b�N�s�����A���̓T�C�Y�𒲐߂���
		if(block_pos_y == block_num_y - 1) {
			output_size_y = fpip->h - block_pos_y * block_size_y;
			input_size_y = output_size_y + kSteps * 2;
		}
		// �������ɂ�������̓T�C�Y(input_size_x)�Əo�̓T�C�Y(output_size_x)�����肷��
		auto input_size_x = block_size_x + kSteps * 2;
		if(input_size_x % SIMD != 0) input_size_x += SIMD - (input_size_x % SIMD);
		auto input_size_x_SIMD = input_size_x / SIMD;
		auto output_size_x = block_size_x;
		for(auto block_pos_x = 0; block_pos_x < block_num_x; ++block_pos_x){
			// ��ԉE�̃u���b�N�񂾂��A���̓T�C�Y�𒲐߂���
			if(block_pos_y == block_num_y - 1) {
				output_size_x = fpip->w - block_pos_x * block_size_x;
				input_size_x = output_size_x + kSteps * 2;
				if(input_size_x % SIMD != 0) input_size_x += SIMD - (input_size_x % SIMD);
				input_size_x_SIMD = input_size_x / SIMD;
			}
			/* ���͕��� */
			float *input_picture_y = (float*)_mm_malloc(sizeof(float) * kMaxInput * input_size_y * input_size_x, alignment_size);
			if(input_picture_y == NULL){
				_mm_free(padded_picture);
				throw 0;
			}
			for(auto y = 0; y < input_size_y; y++) {
				for(auto x = 0; x < input_size_x; ++x) {
					input_picture_y[y * input_size_x + x] = padded_picture[(block_pos_y * block_size_y + y) * padded_picture_x + (block_pos_x * block_size_x + x)];
				}
			}
			/* ���Z���� */
			// �c�T�C�Y�̓X�e�b�v����2�Â����Ă��������T�C�Y�͌���Ȃ��B
			// ����́A���T�C�Y��܊pSIMD�����ɏ������Ŋ���؂��悤�ɂ����̂ɒׂ��ꂽ���Ȃ����߁B
			PackedFloat *output_picture_y = (PackedFloat*)_mm_malloc(sizeof(PackedFloat) * kMaxOutput * input_size_y * input_size_x_SIMD, alignment_size);
			if(output_picture_y == NULL){
				_mm_free(padded_picture);
				_mm_free(input_picture_y);
				throw 0;
			}
			auto input_size_y_ = input_size_y - 2;
			for(auto s = 0; s < kSteps; ++s) {
				Step *step = &g_model_data[mode_].step[s];
				auto input_plane_size = step->input_plane_size;
				auto output_plane_size = step->output_plane_size;
				// �o�͕��ʂ�����������
				for(auto o = 0; o < output_plane_size; ++o) {
					for(auto y = 0; y < input_size_y_; ++y) {
						for(auto x = 0; x < input_size_x_SIMD; ++x) {
							output_picture_y[(o * input_size_y + y) * input_size_x_SIMD + x] = PackedSetZero();
						}
					}
				}
				// �o�͕��ʂ𐶐�����
#pragma omp parallel for num_threads(thread_)
				for(auto o = 0; o < output_plane_size; ++o) {
					// ��ݍ��݉��Z����
					for(auto i = 0; i < input_plane_size; ++i) {
						for(auto y = 0; y < input_size_y_; ++y) {
							for(auto x = 0, x_SIMD = 0; x < input_size_x; x += SIMD, ++x_SIMD) {
								// �ǂݍ���
								PackedFloat input_simd[kFilterSize];
								PackedFloat *weight = step->weight_simd[o][i];
								input_simd[0 * kWidthSize + 0] = PackedLoad(&input_picture_y[(i * input_size_y + (y + 0)) * input_size_x + (x + 0)]);
								input_simd[0 * kWidthSize + 1] = PackedLoad(&input_picture_y[(i * input_size_y + (y + 0)) * input_size_x + (x + 1)]);
								input_simd[0 * kWidthSize + 2] = PackedLoad(&input_picture_y[(i * input_size_y + (y + 0)) * input_size_x + (x + 2)]);
								input_simd[1 * kWidthSize + 0] = PackedLoad(&input_picture_y[(i * input_size_y + (y + 1)) * input_size_x + (x + 0)]);
								input_simd[1 * kWidthSize + 1] = PackedLoad(&input_picture_y[(i * input_size_y + (y + 1)) * input_size_x + (x + 1)]);
								input_simd[1 * kWidthSize + 2] = PackedLoad(&input_picture_y[(i * input_size_y + (y + 1)) * input_size_x + (x + 2)]);
								input_simd[2 * kWidthSize + 0] = PackedLoad(&input_picture_y[(i * input_size_y + (y + 2)) * input_size_x + (x + 0)]);
								input_simd[2 * kWidthSize + 1] = PackedLoad(&input_picture_y[(i * input_size_y + (y + 2)) * input_size_x + (x + 1)]);
								input_simd[2 * kWidthSize + 2] = PackedLoad(&input_picture_y[(i * input_size_y + (y + 2)) * input_size_x + (x + 2)]);
								// ���Z�E��������
								PackedFloat *temp_simd = &output_picture_y[(o * input_size_y + y) * input_size_x_SIMD + x_SIMD];
								*temp_simd = PackedFMA(weight[0], input_simd[0], *temp_simd);
								*temp_simd = PackedFMA(weight[1], input_simd[1], *temp_simd);
								*temp_simd = PackedFMA(weight[2], input_simd[2], *temp_simd);
								*temp_simd = PackedFMA(weight[3], input_simd[3], *temp_simd);
								*temp_simd = PackedFMA(weight[4], input_simd[4], *temp_simd);
								*temp_simd = PackedFMA(weight[5], input_simd[5], *temp_simd);
								*temp_simd = PackedFMA(weight[6], input_simd[6], *temp_simd);
								*temp_simd = PackedFMA(weight[7], input_simd[7], *temp_simd);
								*temp_simd = PackedFMA(weight[8], input_simd[8], *temp_simd);
							}
						}
					}
					// �o�C�A�X���|����
					for(auto y = 0; y < input_size_y_; ++y) {
						for(auto x = 0; x < input_size_x_SIMD; ++x) {
							PackedFloat *temp_simd = &output_picture_y[(o * input_size_y + y) * input_size_x_SIMD + x];
							*temp_simd = PackedAdd(*temp_simd, step->bias[o]);
						}
					}
				}
				// �o�͕��ʂ���͕��ʂɔ��f����
				for(auto o = 0; o < output_plane_size; ++o) {
					for(auto y = 0; y < input_size_y_; ++y) {
						for(auto x = 0; x < input_size_x_SIMD; ++x) {
							// �u�����̂�0.1�{�v��SIMD�ō��������Ă���
							PackedFloat *temp_simd = &output_picture_y[(o * input_size_y + y) * input_size_x_SIMD + x];
							PackedFloat lt_zero = PackedCmpLt(*temp_simd, kZeroSIMD);	//�e�v�f�ɂ���0.0�����Ȃ�0xFFFFFFFF�A�łȂ���0�ɂ���
							*temp_simd = PackedBrend(*temp_simd, PackedMul(*temp_simd, kConstSIMD), lt_zero);
							PackedStore(&input_picture_y[(o * input_size_y + y) * input_size_x + (x * SIMD)], *temp_simd);
						}
					}
				}
				input_size_y_ -= 2;
			}
			/* �o�͕��� */
			for(auto y = 0; y < output_size_y; ++y) {
				int y_ = block_pos_y * block_size_y + y;
				auto ycp = fpip->ycp_edit + y_ * fpip->max_w + block_pos_x * block_size_x;
				for(auto x = 0; x < output_size_x; ++x) {
					ycp->y = static_cast<short>(round(input_picture_y[y * input_size_x + x] * 4096));
					ycp++;
				}
			}
			/* ��������������� */
			_mm_free(output_picture_y);
			_mm_free(input_picture_y);
		}
	}

	/* �Y�ꂸ�Ƀ�������������� */
	_mm_free(padded_picture);
	return;
}