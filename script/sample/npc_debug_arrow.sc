//
// ���X�L�����ł���܂ł̊Ԃ́A�|��g���~��NPC�Ɏg���Ă���ĉ����� (^^;
// �����P�ɓXNPC����l�܂�Ȃ��̂ŁA�ȒP�ȃC�x���g�ɁB
// �l�i�́A�K���f�X�B	write by huge
//

payon.gat,98,57,6	script	���E���B	86,{
	if(getbaseclass(Class) != CLASS_AC && getbaseclass(Class) != CLASS_TF) {
		mes "[���E���B]";
		mes "�₟�A���߂܂��āB���l����B";
		mes "�������V�C�������ł��˂��B";
		mes "��̗�������₩���B";
		close;
	}
	if(ARROW_NPC == 0) {
		mes "[���E���B]";
		mes "�₟�A���߂܂��āB���l����B";
		mes "����A���Ȃ��͋|�g���̂悤�ł��ˁB";
		mes "�����́A�n���^�[��������ł���B";
		next;
		mes "[���E���B]";
		mes "�ł��A�Ƃ��鎖��őސE���܂����B";
		mes "���̍��́A�������y���������Ȃ��E�E�E�B";
		mes "���A�������I�B";
		next;
		mes "[���E���B]";
		mes "�����̎g���Ă�����̗]���";
		mes "���Ȃ��ɂ����܂��傤�B";
		mes "�����A�����ސE�����g�Ȃ̂ŏ��X����͒����܂����B";
		next;
		mes "[���E���B]";
		mes "���̊X�̒����L��ɋ��邨�������";
		mes "�S���a���Ă�̂ŁA�b�������Ă݂Ă��������B";
		mes "�����炠�Ȃ��̎���`���Ă����܂�";
		mes "�̂ŁB";
		set ARROW_NPC,1;
		close;
	}
	mes "[���E���B]";
	mes "�₟�A�܂���܂����ˁB���l����B";
	mes "�o�h���b�N��������ɂ͉�܂����H";
	mes "���̊X�̒����L��ɋ��܂���B";
	close;
}

payon.gat,100,121,3	script	�o�h���b�N	85,{
	if(ARROW_NPC == 0) {
		if(Sex) {
			mes "[�o�h���b�N]";
			mes "�N�����O�́I";
			mes "�C�����b�������񂶂�˂��I";
			mes "���́A�j�ɂ͋����͂˂��񂾂�B";
			mes "�������Ɨ�������ȁI";
		}
		else {
			mes "[�o�h���b�N]";
			mes "�悧�A�˂������B";
			mes "��������ƈ�t���Ȃ������H";
		}
		close;
	}
	mes "[�o�h���b�N]";
	mes "���񂽂����E���B�̒m�荇�����Ă̂́B";
	mes "�A�C�c�́A���[�����Ă����\�Șr�łȁA";
	mes "�����̍��́A�|�g���ł��̖���m��Ȃ�";
	mes "��͋��Ȃ����Ă��炢���������B";
	mes "������A���E���̖�����Ă₪�邺�I";
	next;
	mes "[�o�h���b�N]";
	mes "����A�����������I��ł���B";
	mes "�������y������������܂Ƃߔ��肾���B";
	next;
	switch(select("����Ȃ�",
	              "��~1000 -> 1,000z",
	              "��΂̖�~500 -> 1,500z",
	              "�K�т���~500 -> 1,500z",
	              "�s����~500 -> 1,500z",
	              "�S�̖�~500 -> 1,500z",
	              "�|�S�̖�~500 -> 1,500z",
	              "��̖�~500 -> 1,500z",
	              "���̖�~500 -> 2,500z",
	              "�X�̖�~500 -> 2,500z",
	              "���̖�~500 -> 2,500z",
	              "�����̖�~500 -> 2,500z",
	              "���`�̖�~500 -> 2,500z",
	              "�Ŗ�~500 -> 3,500z",
	              "�J�[�X�A���[�~500 -> 3,500z",
	              "�X�^���A���[�~500 -> 3,500z",
	              "�X���[�v�A���[�~500 -> 3,500z",
	              "�t���b�V���A���[�~500 -> 3,500z",
	              "�T�C�����X�A���[�~500 -> 3,500z",
	              "�e��~500 -> 3,500z",
	              "�j����~100 -> 40,000z",
	              "�I���f�I�R���̖�~100 -> 50,000z")) {
	case 1:		// cancel
		mes "[�o�h���b�N]";
		mes "�����Ȃ�ŏ�����b��������ȁI";
		close;
	case 2:		// ��
		set '@zeny,1000;
		set '@itemid,1750;
		set '@amount,1000;
		break;
	case 3:		// ��΂̖�
		set '@zeny,1500;
		set '@itemid,1756;
		set '@amount,500;
		break;
	case 4:		// �K�т���
		set '@zeny,1500;
		set '@itemid,1762;
		set '@amount,500;
		break;
	case 5:		// �s����
		set '@zeny,1500;
		set '@itemid,1764;
		set '@amount,500;
		break;
	case 6:		// �S�̖�
		set '@zeny,1500;
		set '@itemid,1770;
		set '@amount,500;
		break;
	case 7:		// �|�S�̖�
		set '@zeny,1500;
		set '@itemid,1753;
		set '@amount,500;
		break;
	case 8:		// ��̖�
		set '@zeny,1500;
		set '@itemid,1751;
		set '@amount,500;
		break;
	case 9:		// ���̖�
		set '@zeny,2500;
		set '@itemid,1752;
		set '@amount,500;
		break;
	case 10:	// �X�̖�
		set '@zeny,2500;
		set '@itemid,1759;
		set '@amount,500;
		break;
	case 11:	// ���̖�
		set '@zeny,2500;
		set '@itemid,1755;
		set '@amount,500;
		break;
	case 12:	// �����̖�
		set '@zeny,2500;
		set '@itemid,1754;
		set '@amount,500;
		break;
	case 13:	// ���`�̖�
		set '@zeny,2500;
		set '@itemid,1757;
		set '@amount,500;
		break;
	case 14:	// �Ŗ�
		set '@zeny,3500;
		set '@itemid,1763;
		set '@amount,500;
		break;
	case 15:	// �J�[�X�A���[
		set '@zeny,3500;
		set '@itemid,1761;
		set '@amount,500;
		break;
	case 16:	// �X�^���A���[
		set '@zeny,3500;
		set '@itemid,1758;
		set '@amount,500;
		break;
	case 17:	// �X���[�v�A���[
		set '@zeny,3500;
		set '@itemid,1768;
		set '@amount,500;
		break;
	case 18:	// �t���b�V���A���[
		set '@zeny,3500;
		set '@itemid,1760;
		set '@amount,500;
		break;
	case 19:	// �T�C�����X�A���[
		set '@zeny,3500;
		set '@itemid,1769;
		set '@amount,500;
		break;
	case 20:	// �e��
		set '@zeny,3500;
		set '@itemid,1767;
		set '@amount,500;
		break;
	case 21:	// �j����
		set '@zeny,40000;
		set '@itemid,1766;
		set '@amount,100;
		break;
	case 22:	// �I���f�I�R���̖�
		set '@zeny,50000;
		set '@itemid,1765;
		set '@amount,100;
		break;
	}
	if(Zeny < '@zeny) {
		mes "[�o�h���b�N]";
		mes "�������������悤���ȁB�B";
		mes "�o�����Ă�����B";
		mes "���͂��ł������ɋ��邩��ȁB";
		close;
	}
	if(!checkweight('@itemid,'@amount)) {
		mes "[�o�h���b�N]";
		mes "�d�ʂ��y�����Ă��痈���B";
		close;
	}
	set Zeny,Zeny-'@zeny;
	getitem '@itemid,'@amount;
	mes "[�o�h���b�N]";
	mes "�܂��ǂ���[�B";
	close;
}