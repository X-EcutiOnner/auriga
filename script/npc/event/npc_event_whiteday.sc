//==============================================================================
// Ragnarok Online Whiteday2006 Script     by Blaze
//==============================================================================
alberta.gat,26,243,3	script	�V���R��=�o�V�j�I	96,{
	if((Weight*100/Maxweight) >= 90) {
		mes "-�����A�C�e���̏d�ʂ���������";
		mes "�A�C�e�����󂯂Ƃ邱�Ƃ��ł��܂���-";
		mes "-�����A�C�e�������炵�Ă���A�ēx";
		mes "�b�������Ă�������-";
		close;
	}
	switch(QUEST_WHITEDAY) {
	case 0:
		mes "[�V���R��=�o�V�j�I]";
		mes "����ɂ��́`�I";
		mes "�^�S�ƈ��ŗ��l�����̂��߂�";
		mes "�`���R���[�g�����V���R���ł��B";
		next;
		if(select("�`���R���[�g�͂���܂����H","�����ǂ����Ƃł���������ł����H")==2) {
			mes "[�V���R��=�o�V�j�I]";
			mes "�ǂ����Ƃł�����!?";
			mes "���͂͂͂͂́c�c";
			mes "�ӂӂӂӂӁc�c";
			mes "�����͂͂͂́c�c";
			mes "�͂��c�c";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "�i�����������Ƃł��������̂��ȁH�j";
			mes "�Ƃ���Ń`���R���[�g��";
			mes "����܂����H";
			next;
		}
		mes "[�V���R��=�o�V�j�I]";
		mes "���߂�Ȃ����B";
		mes "���`���R���[�g�͖�����ł��B";
		mes "�o�����^�C���̂Ƃ���";
		mes "�J�J�I������Ȃ��đ�ςł�����";
		mes "���x�͓D�_���c�c";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�D�_�H";
		mes "������������ł����H";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "�D�_�Ȃ̂��ǂ���";
		mes "���ɂ��ǂ�������Ȃ��̂ł��B";
		mes "���镪�����u�����Ă����ƁA";
		mes "���̓��ɂ͕K��";
		mes "�ǂ����ɏ����Ă��܂���ł���B";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "����ƁA�s�v�c�Ȃ��Ƃ�";
		mes "�����ɒN���������Ă���";
		mes "�`�Ղ��Ȃ��̂ł��B";
		mes "������Ɩڂ𗣂������ɁA";
		mes "�����Ă��܂����Ƃ�����̂ł���B";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "�N�����ςȖ��@�Ƃ���";
		mes "�����Ă���̂�������܂���c�c";
		mes "�Ȃ̂ŁA���͏������o���Ȃ���ł��B";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "�������c�c";
		mes "����Ă͏����A����Ă͏����c�c";
		mes "�{���ɕs�v�c�ł��c�c";
		mes "�����A�{���ɏ������~�߂Ă��܂�����";
		mes "�v���Ă����Ƃ���ł����B";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "�͂͂́c�c";
		mes "�{���ɐ\����Ȃ��̂ł�����";
		mes "�����͂�����肭�������c�c";
		set QUEST_WHITEDAY,1;
		close;
	case 1:
		mes "[�V���R��=�o�V�j�I]";
		mes "�������c�c";
		mes "�`���R���[�g������Ă��邾���ł��̂�";
		mes "�N�����킩��Ȃ��ł�����";
		mes "�{���ɍ����ł���ˁc�c";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "����ɁA�`���R���[�g�����Ȃ����";
		mes "�v�����e���̃A���������";
		mes "�����Ă��܂��͂��ł��c�c";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "�����Ȃ�����A�ޗ����Ȃ��Ȃ�܂�";
		mes "�Ƃ��Ƃ����Ă��������܂���!!";
		mes "�c�c";
		mes "���������c�c";
		mes "��قǍ�����΂���̃`���R���[�g��";
		mes "�Ȃ��Ȃ��Ă܂����c�c";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "���I�@�������I";
		mes "�����A��낵�����";
		mes "��������`���R���[�g��";
		mes "�v�����e���̃A���������";
		mes "�͂��Ă��������܂��񂩁H";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "���܂�Ă��܂��O�ɁA";
		mes "�A��������ɂ��͂��������̂ł��B";
		mes "���肢���܂��c�c";
		next;
		if(select("�����ł���","���[��A���͖Z����")==2) {
			mes "[�V���R��=�o�V�j�I]";
			mes "�������c�c";
			mes "�ł��A�Z�����̂ł�����";
			mes "�d���Ȃ��ł��ˁc�c";
			close;
		}
		mes "[�V���R��=�o�V�j�I]";
		mes "���肪�Ƃ��������܂��I";
		mes "�A��������ɂ�";
		mes "���f�������Ă��܂����̂�";
		mes "����͂���Ȃ��Ɠ`���Ă��������B";
		mes "���ƁA����̐��������˂������܂��B";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�킩��܂����B";
		mes "���i�������Ă��܂��b�ł��ˁB";
		mes "�Ƃ���Ń`���R���[�g�͂ǂ��ɁH";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "���A����ł��B";
		mes "����Ă������̂�";
		mes "���̂܂ܓn���Ă��������B";
		mes "����ł́A���肢���܂��`�I";
		set QUEST_WHITEDAY,2;
		getitem 7432,1;
		close;
	case 2:
		mes "[�V���R��=�o�V�j�I]";
		mes "�A��������́A�v�����e�����̕��ɂ���";
		mes "����ɂ��܂��B";
		mes "���A���X�g�����Ȃ�ł��傤���H";
		mes "����ł͂��肢���܂��B";
		close;
	default:
		mes "[�V���R��=�o�V�j�I]";
		mes "���肪�Ƃ��������܂����`";
		mes "�A�������񂩂畷���܂�����I";
		mes "�`���R���[�g����������";
		mes "�͂��Ă��ꂽ�̂ł����ĂˁB";
		mes "�ł��A���̂܂܂���c�c";
		mes "�����ɂȂ�Ȃ��ł��ˁc�c";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "�������c�c";
		mes "�ǂ����悤�A�ǂ����悤�`!!";
		mes "���т��H�ׂ��Ȃ��Ȃ��Ă��܂��c�c";
		mes "���c�c���A���߂�Ȃ����c�c";
		mes "�������Ă��܂��܂����c�c";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "�A���������";
		mes "���i�������Ă��܂����Ƃɑ΂��āA";
		mes "����������ƌ����Ă��܂������c�c";
		mes "�ǂ����ʂ��o��΂����̂ł����ǁc�c";
		mes "�͂����c�c";
		close;
	case 6:
		mes "[�V���R��=�o�V�j�I]";
		mes "��������Ⴂ�܂��`�I�@�h�{���_�I";
		mes "�������ς��I�@�K���S�{�I";
		mes "�u�o�V�j�I�`���R���[�g�v�ł��I";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "���I�@�����̃e���V�����������Ȃ���";
		mes "�v���܂����ˁH";
		mes "����!!!!";
		mes "���́A�`���R���[�g�j���t��";
		mes "��Ă�悤�ɂȂ����̂ł���I";
		next;
		mes "[�V���R��=�o�V�j�I]";
		mes "�ӂӁA��������������ł���I";
		mes "�������A�������`���R���[�g��";
		mes "�S���������̂�����Ă�����ł��I";
		mes "���V�[�Y���͂��̎q�ɂ��C���ł��I";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�i�Ȃ񂾁c�c�A���ꂩ��͓D�_�����Ă�";
		mes "���܂�Ȃ����Ă��ƂȂ̂��ȁc�c�j";
		mes "�͂͂͂��c�c";
		mes "���̃j���t�{���ɉ����ł��ˁB";
		mes "�c�c";
		close;
	}
}

prt_in.gat,41,40,3	script	�A����=�I�����A��	86,{
	if((Weight*100/Maxweight) >= 90) {
		mes "-�����A�C�e���̏d�ʂ���������";
		mes "�A�C�e�����󂯂Ƃ邱�Ƃ��ł��܂���-";
		mes "-�����A�C�e�������炵�Ă���A�ēx";
		mes "�b�������Ă�������-";
		close;
	}
	switch(QUEST_WHITEDAY) {
	case 0:
	case 1:
		mes "[�A����=�I�����A��]";
		mes "�ςł��ˁc�c";
		mes "�����ɏ�������ł��傤�c�c";
		mes "�܂����A�N���̌����点�ł��傤���H";
		next;
		mes "[�A����=�I�����A��]";
		mes "���A���q�l�c�c";
		mes "�\���󂠂�܂��񂪁A���i���Ȃ���";
		mes "�����肷�鎖���o���Ȃ���ł��B";
		mes "����ł��������i���A";
		mes "�͂��ĂȂ���ł���B";
		next;
		mes "[�A����=�I�����A��]";
		mes "�{���ɐ\���󂠂�܂���B";
		mes "�܂��A���Ă��������܂����c�c";
		close;
	case 2:
		if(countitem(7432)==0) {
			mes "[�A����=�I�����A��]";
			mes "���������c�c";
			mes "�o�V�j�I�`���R���[�g���͂��Ȃ��c�c";
			mes "���܂Ŕz�B���x���Ȃ�Ă���";
			mes "�Ȃ������̂Ɂc�c";
			mes "��x�A������ꂽ�ق���";
			mes "�悳�����ł��ˁc�c";
			next;
			mes "[�A����=�I�����A��]";
			mes "���I�@���q����c�c";
			mes "�\���󂠂�܂��񂪁A���i���Ȃ���";
			mes "�����肷�鎖���o���Ȃ���ł��B";
			mes "����ł��������i���A";
			mes "�͂��ĂȂ��āc�c";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "���A���̂��ƂȂ�ł�����";
			mes "�V���R������ɗ��܂�āc�c";
			mes "�i����H�@�n���ꂽ";
			mes "�`���R���[�g������!?";
			mes "��ς��c�c�ǂ����悤�c�c�j";
			next;
			mes "[�A����=�I�����A��]";
			mes "�����A����ŃV���R������͂Ȃ�ƁH";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "���A�����A�ԈႢ�ł����c�c";
			mes "���ł�����܂���c�c";
			mes "�i�����ɍs�����񂾂낤�H";
			mes "�Ƃɂ����T���Ȃ��Ɓc�c�j";
			next;
			mes "[�A����=�I�����A��]";
			mes "�����ł����c�c";
			mes "�͂��c�c";
			close;
		}
		mes "[�A����=�I�����A��]";
		mes "�����p�ł����H";
		mes "������ƍ��͖Z�����āc�c";
		mes "�\����Ȃ��̂ł���";
		mes "���ɗp�����Ȃ��悤�ł�����";
		mes "������肢�������܂����H";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "������A�V���R�����񂩂�";
		mes "�a�����Ă��܂����B";
		next;
		mes "[�A����=�I�����A��]";
		mes "���H�@�{���ł����H";
		mes "�c�c";
		mes "���I�@����̓o�V�j�I�`���R���[�g�I";
		mes "����ł��A�����҂��Ă�����ł���I";
		mes "�悩�����[�I";
		next;
		mes "[�A����=�I�����A��]";
		mes "�ł��c�c";
		mes "���̂���Ȃɒx��������ł��傤�H";
		mes "���܂ł͔[�i���x���Ȃ�Ă���";
		mes "�Ȃ�������ł����ǁB";
		mes "�܂���!!";
		mes "�V���R������ɉ�����������ł���!?";
		next;
		mes "-�[�i���x�ꂽ������������-";
		next;
		mes "[�A����=�I�����A��]";
		mes "�Ȃ�قǁc�c";
		mes "�����������Ƃ������̂ł��ˁB";
		mes "�ł��c�c�ςł��ˁc�c";
		mes "���́A���������悤�Ȃ��Ƃ�";
		mes "��������ł���B";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�����悤�Ȃ��ƁH";
		mes "�ǂ�Ȃ��Ƃł����H";
		next;
		mes "[�A����=�I�����A��]";
		mes "���͂ł��ˁc�c";
		mes "������Ɩڂ��͂Ȃ�������";
		mes "���蕨�̃`���R���[�g��";
		mes "�S�������Ă��܂�����ł���B";
		mes "���������Ȃ������ł����c�c";
		next;
		mes "[�A����=�I�����A��]";
		mes "����ŁA�`���R���[�g��";
		mes "�Ȃ��Ȃ����ꏊ��";
		mes "����Ȃ��̂������Ă�����ł��B";
		mes "�����A�D�_�̎d�ƂȂ�";
		mes "����Ɗ֌W������̂ł��傤���H";
		next;
		mes "-�A����=�I�����A����������";
		mes "���ꂽ�̂́A�����ȘI������-";
		next;
		mes "[�A����=�I�����A��]";
		mes "����́A�u�C�O�h���V���̘I�v";
		mes "�ƌĂ΂����̂ŁA";
		mes "�A���P�~�X�g�̃X�L��";
		mes "�z�����N���X�Ɏg�����̂ł��B";
		next;
		mes "[�A����=�I�����A��]";
		mes "�����N���̎d�ƂȂ�";
		mes "�A���P�~�X�g���Ɛl�Ƃ����\����";
		mes "�����Ǝv����ł���B";
		mes "�N���A���̑����";
		mes "�������Ă����l�������";
		mes "������̂ł����ǁc�c";
		next;
		mes "[�A����=�I�����A��]";
		mes "���A�����ł����B";
		mes "�z�B�����Ă���āA";
		mes "���肪�Ƃ��������܂����B";
		mes "����̂��ƂɊւ��ẮA";
		mes "�V���R������Ƙb���Ă����܂��ˁB";
		set QUEST_WHITEDAY,3;
		close;
	case 3:
		mes "[�A����=�I�����A��]";
		mes "��͂�A�Ɛl�̓A���P�~�X�g�ł���";
		mes "�\���������Ǝv����ł��B";
		mes "�A���f�o������";
		mes "�A���P�~�X�g�M���h�ɍs����";
		mes "�������Ȃ���΂Ȃ�܂���I";
		next;
		mes "[�A����=�I�����A��]";
		mes "�ł��c�c";
		mes "���͏���������܂����A";
		mes "�����𓮂��킯�ɂ͂����Ȃ��c�c";
		mes "�N�����ׂĂ����l�������";
		mes "�{���ɏ�����̂ł����ǁc�c";
		next;
		if(select("��������ɒ��ׂ܂��傤���H","�̔��͂�����ĊJ����̂ł����H")==2) {
			mes "[�V���R��=�o�V�j�I]";
			mes "���[�A�����ł��ˁc�c";
			mes "���̎����̐^���𖾂炩�ɂ���܂ł�";
			mes "�̔��͂ނ��������ł��傤�c�c";
			mes "�z���C�g�f�[���Ԃ��I���O��";
			mes "�҂��Ȃ��Ƃ����Ȃ��̂ł����c�c";
			next;
			mes "[�V���R��=�o�V�j�I]";
			mes "���͋M���̂悤��";
			mes "�`���҂ł�����܂���";
			mes "�����Ă����l�����Ȃ��̂Łc�c";
			next;
			mes "[�V���R��=�o�V�j�I]";
			mes "���͕ʂɑ傻�ꂽ��]��";
			mes "�����Ă���킯�ł͂Ȃ���ł��B";
			mes "�������Đ�������҂���΂����B";
			mes "�����A���ꂾ���Ȃ�ł��c�c";
			mes "����Ȃ̂ɁA���ł���Ȃ��ƂɁc�c";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "���C�̓łɁc�c";
			mes "���ɂȂ邩������Ȃ��̂ł��ˁB";
			next;
			mes "[�V���R��=�o�V�j�I]";
			mes "�����ł��ˁc�c";
			mes "���ɂȂ��ł��傤���c�c";
			mes "�z���C�g�f�[�V�[�Y����";
			mes "�I�������ł����ˁH";
			mes "���͂͂͂́c�c";
			close;
		}
		mes "[�A����=�I�����A��]";
		mes "�{���ł���!?";
		mes "���肪�Ƃ��������܂��I";
		mes "���������������Ƃ���A";
		mes "�A���P�~�X�g�M���h�́A";
		mes "�A���f�o�����ɂ���܂��B";
		next;
		mes "[�A����=�I�����A��]";
		mes "���I�@�������킩��܂�����A";
		mes "�����Ă��������܂����H";
		mes "�������Ă����������Ȃ�";
		mes "������񂨗�͂������܂���B";
		mes "����ł́A��낵�����肢���܂��B";
		set QUEST_WHITEDAY,4;
		close;
	case 4:
	case 5:
		mes "[�A����=�I�����A��]";
		mes "�A���P�~�X�g�Ƃ�����";
		mes "��͂�A�A���f�o�����ł��ˁB";
		mes "�ǂ����ʂ�S�҂��ɂ��Ă��܂��B";
		close;
	case 6:
		mes "[�A����=�I�����A��]";
		mes "���I�@�����O�ɓD�_�Ɋւ��Ă�";
		mes "���������肢�������ł��ˁB";
		mes "���̐߂͂��肪�Ƃ��������܂����I";
		mes "�ł��A���͂ǂ��ł��������ƂȂ�ł��B";
		next;
		mes "[�A����=�I�����A��]";
		mes "���͂ł��ˁc�c";
		mes "�M���Ă���邩������܂���";
		mes "�`���R���[�g�j���t��";
		mes "��ĂĂ����ł���I";
		next;
		mes "[�A����=�I�����A��]";
		mes "�͂��͂���!!";
		mes "���̎q��������ł���I";
		mes "�o�V�j�I�`���R���[�g�ƑS������";
		mes "�`���R���[�g������ł��I";
		next;
		mes "[�A����=�I�����A��]";
		mes "�����J�b�v��������";
		mes "���т����C�����ɂȂ��Ă�������";
		mes "����Ȃɒg�����C������";
		mes "���Ă�Ȃ�āc�c";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�i�Ȃ񂾁c�c�A���ꂩ��͓D�_�����Ă�";
		mes "���܂�Ȃ����Ă��ƂȂ̂��ȁc�c�j";
		mes "�͂͂͂��c�c";
		mes "���̃j���t�{���ɉ����ł��ˁB";
		mes "�c�c";
		next;
		getinventorylist;
		for(set '@i,0; '@i<@inventorylist_count; set '@i,'@i+1) {
			if(@inventorylist_id['@i] == 559 || @inventorylist_id['@i] == 560) {
				if(@inventorylist_card3['@i]==0 && @inventorylist_card4['@i]==0) {
					mes "[�A����=�I�����A��]";
					mes "�����I";
					mes "�f�G�ȃ`���R���[�g�������Ă��܂��ˁI";
					mes "����̓T�[�r�X�ł��I";
					delitem @inventorylist_id['@i],1;
					getitem2 @inventorylist_id['@i],1,1,0,0,254,0,getcharid(0)&0xffff,(getcharid(0)>>16)&0xffff;
					next;
					break;
				}
			}
		}
		mes "[�A����=�I�����A��]";
		mes "�͂��͂͂͂�!!";
		close;
	}
}

aldebaran.gat,155,88,3	script	�b��������	729,{
	if((Weight*100/Maxweight) >= 90) {
		mes "-�����A�C�e���̏d�ʂ���������";
		mes "�A�C�e�����󂯂Ƃ邱�Ƃ��ł��܂���-";
		mes "-�����A�C�e�������炵�Ă���A�ēx";
		mes "�b�������Ă�������-";
		close;
	}
	switch(QUEST_WHITEDAY) {
	default:	//0�`4�̂Ƃ�
		mes "[�`�F���[�x���[]";
		mes "����͐�����������I";
		mes "�J�̌�c�c����͂����Łc�c";
		mes "��H�@�����̂��Ȃ��I";
		mes "�l�̘b�𓐂ݕ�������Ȃ�Ď���ˁI";
		if(QUEST_WHITEDAY<4) close;
		next;
		if(select("������������ł����H","���炵�܂����c�c")==2) {
			mes "[�`�F���[�x���[]";
			mes "�ςȐl�c�c";
			close;
		}
		mes "[�`�F���[�x���[]";
		mes "�ӂӂӁA���z�����N���X�̌��������Ă�";
		mes "���������������́I";
		mes "���Â��āI�@�`���R���[�g�j���t�I";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�`���R���[�g�j���t�H";
		mes "�c�c";
		mes "�i���̎q�A�������`���R���[�g��";
		mes "�����֌W������̂��ȁH�j";
		next;
		mes "[�`�F���[�x���[]";
		mes "���Ȃ�����ĂĂ݂�H";
		mes "��Ă�Ȃ�A�������ʂɗp�ӂ���";
		mes "�j���t�̎���������I";
		next;
		if(select("���������I","�R�ł��傤�H")==2) {
			mes "[�`�F���[�x���[]";
			mes "�Ӂ[��A�M���ĂȂ��񂾁B";
			mes "���ꂶ��A�����Ȃ����B";
			close;
		}
		mes "[�`�F���[�x���[]";
		mes "���ӂӂӁc�c";
		mes "����ŁA�o�����^�C���A�z���C�g�f�[";
		mes "�֌W�����ɁA���l�����Ȃ�������";
		mes "�`���R���[�g��";
		mes "���`��������H�ׂ����I";
		next;
		mes "[�`�F���[�x���[]";
		mes "���l�����Ȃ�������";
		mes "�N�ɂł��`���R���[�g��";
		mes "���炤����������I";
		mes "���Ȃ��������v���ł���H";
		mes "�v�����!!!?";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�c�c";
		mes "�܁A�܂�������������܂���ˁc�c";
		mes "�i�������Ȃ��Ǝv�����ǁc�c�j";
		next;
		mes "[�`�F���[�x���[]";
		mes "�����A���߂�Ȃ����B";
		mes "�b�������Ȃ��Ă��܂�����B";
		mes "�����j���t�̎��";
		mes "�}�j���A����������ˁB";
		next;
		set QUEST_WHITEDAY,5;
		set QUEST_WHITEDAY_POSITION,rand(1,9);
		getitem 7182,1;
		mes "[�`�F���[�x���[]";
		mes "�̐S�Ȃ��Ƃ������Y��Ă�����A";
		mes "���A����ꏊ�Ȃ񂾂���";
		mes "�`���R���[�g�j���t�͐X��";
		mes "��D���Ȃ̂�ˁB";
		mes "������A�t�F�C�����Ƃ�";
		mes "�A���x���^�t�߂̐X�ɍs���Ă݂āB";
		next;
		mes "[�`�F���[�x���[]";
		mes "���A����̂ɍœK�ȏꏊ�ɋ߂Â���";
		mes "�}�j���A���K�C�h��";
		mes "�����I�ɔ������āA�����Ă���邩��A";
		mes "���̌�̓K�C�h�̎w���ɁA";
		mes "�]���΂�����B";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "������܂����B";
		mes "�i�܂��A�x���ꂽ�Ǝv����";
		mes "��x����Ă݂悤�B";
		mes "�t�F�C�������A���x���^";
		mes "�ߗׂ̐X���c�c�j";
		close;
	case 5:
		mes "[�`�F���[�x���[]";
		mes "���q�͂ǂ��H";
		close;
	case 6:
		if(QUEST_WHITEDAY_POSITION) {
			mes "[�`�F���[�x���[]";
			mes "�ӂӂӁc�c";
			mes "���ꂾ���`���R���[�g��z���";
			mes "�z���C�g�f�[�Ȃ�āA";
			mes "���ʂȓ�����Ȃ��Ȃ���ˁB";
			mes "�c�c�N�b�N�b�N�b�N�c�c";
			close;
		}
		mes "[�`�F���[�x���[]";
		mes "�`���R���[�g�j���t�ɉ���Ă݂��H";
		mes "������x�Ăяo���Ă݂�H";
		next;
		if(select("��߂Ă��������I","������x����Ă݂����I")==1) {
			mes "[�`�F���[�x���[]";
			mes "������Ɓ[�c�c";
			mes "�`���R���[�g��ɓ�������ł���H";
			mes "���œ{���Ă�̂�H";
			mes "�Ӗ���������Ȃ���B";
			close;
		}
		mes "[�`�F���[�x���[]";
		mes "�킩������I";
		mes "���ꂶ��A�}�j���A����";
		mes "�j���t�̎�̃J�J�I�̑����";
		mes "25,000Zeny�ˁI";
		mes "�ǂ��H�@�����ł���H";
		next;
		if(select("����܂���","���˂������܂�")==1) {
			mes "[�`�F���[�x���[]";
			mes "���������B";
			mes "�܂��������ǂˁc�c";
			close;
		}
		mes "[�`�F���[�x���[]";
		mes "�ӂӁA����������I";
		next;
		if(Zeny<25000) {
			mes "[�`�F���[�x���[]";
			mes "���Ȃ��c�c";
			mes "����������Ȃ����c�c";
			mes "���ɍޗ�����o���������H";
			close;
		}
		mes "[�`�F���[�x���[]";
		mes "��x����Ă邩��";
		mes "���߂Đ����͂��Ȃ���B";
		mes "���Ƃ́A�}�j���A�����Q�l�ɂ��ĂˁB";
		set QUEST_WHITEDAY_NYMPH,0;
		set QUEST_WHITEDAY_POSITION,rand(1,9);
		getitem 7182,1;
		close;
	}
}

//==============================================================

-	script	WhitedayNavi#0	-1,{
	set '@num,strnpcinfo(2);
	if(QUEST_WHITEDAY_POSITION/3 != '@num+1)	//1�`3�A4�`6�A7�`9��3�p�^�[��
		end;
	mes "^4A4AFF-�����R���p�j�I���V�X�e���쓮-^000000";
	next;
	switch(QUEST_WHITEDAY_POSITION) {
		case 1: viewpoint 1,216,118,1,0x0000FF; break;
		case 2: viewpoint 1,176,120,1,0x0000FF; break;
		case 3: viewpoint 1,192,212,1,0x0000FF; break;
		case 4: viewpoint 1,158,80,1,0x0000FF; break;
		case 5: viewpoint 1,351,106,1,0x0000FF; break;
		case 6: viewpoint 1,194,212,1,0x0000FF; break;
		case 7: viewpoint 1,222,183,1,0x0000FF; break;
		case 8: viewpoint 1,237,349,1,0x0000FF; break;
		case 9: viewpoint 1,61,345,1,0x0000FF; break;
	}
	mes "^4A4AFF����ɂ��́I";
	mes "�`���R���[�g�R���p�j�I���ł��B";
	mes "�܂��A�~�j�}�b�v�����Ă��������B";
	mes "���A����̂ɓK�����ꏊ��";
	mes "�\�����Ă���܂��B^000000";
	next;
	mes "^4A4AFF�~�j�}�b�v�̕\�����m�F����";
	mes "�Y���̏ꏊ�Ɏ��A���Ă��������B^000000";
	close;
}

pay_fild01.gat,216,118,0	duplicate(WhitedayNavi#0)	WhitedayNavi#1	139,3,3
pay_fild03.gat,373,69,0		duplicate(WhitedayNavi#0)	WhitedayNavi#2	139,3,3
pay_fild08.gat,43,77,0		duplicate(WhitedayNavi#0)	WhitedayNavi#3	139,3,3


-	script	�΂�#NYMPH	111,{
	if(QUEST_WHITEDAY_NYMPH==13) {
		mes "-���̃`���R���[�g�j���t��";
		mes "�����ꏊ�ɂ́A�����N�����Ȃ�����-";
		close;
	}
	set '@num,strnpcinfo(2);
	if(QUEST_WHITEDAY<5 || QUEST_WHITEDAY_POSITION != '@num) {
		mes "-�������ȋC�z��������-";
		mes "-�����Ȃ�����������悤��-";
		close;
	}
	switch(QUEST_WHITEDAY_NYMPH) {
	case 0:
		mes "^4A4AFF�{���ɒT���Ă�����ł��ˁB";
		mes "�ł͂��̎�������ɐA���Ă��������B^000000";
		next;
		if(countitem(7182)<1) {
			mes "^4A4AFF�J�J�I��A���Ă��������B^000000";
			close;
		}
		mes "^4A4AFF�J�J�I��A���܂����H";
		mes "�肪�悭�ł�悤��";
		mes "������~���N��������Ƃ����ł���B^000000";
		delitem 7182,1;
		set QUEST_WHITEDAY_NYMPH,1;
		close;
	case 1:
		mes "^4A4AFF�����K�v�Ȃ�ł����A";
		mes "�������~���N�����Ă��܂����H^000000";
		next;
		switch(select("����������","�~���N������","������")) {
			case 1:
				set '@item,523;
				break;
			case 2:
				set '@item,519;
				break;
			case 3:
				mes "^4A4AFF�����Ă邽�߂ɁA";
				mes "�������~���N�������Ă��Ă��������B^000000";
				close;
		}
		mes "^4A4AFF���A�����ꏊ�ɁA";
		mes getitemname('@item)+ "���T���Ă��������B";
		mes "10���炢�����x�ǂ��Ǝv���܂��B";
		mes "�������Ă����Ȃ����Ă�";
		mes "���߂ł�����ˁB^000000";
		next;
		if(countitem('@item) < 10) {
			mes "^4A4AFF" +getitemname('@item)+ "��10�p�ӂ��Ă��������B^000000";
			close;
		}
		mes "["+strcharinfo(0)+"]";
		mes "�Ȃ�ق�" +getitemname('@item)+ "��������΂����񂾂ˁB";
		mes "���Ⴀ�c�c1�c�c2�c�c";
		next;
		misceffect 18,"";
		mes "["+strcharinfo(0)+"]";
		mes "3�c�c4�c�c";
		mes "�܂���͏o�Ȃ��Ȃ��B";
		mes "5�c�c6�c�c";
		next;
		misceffect 18,"";
		mes "["+strcharinfo(0)+"]";
		mes "�c�c�c�c�c�c�c�c";
		mes "�҂��҂��ƌ����Ă�Ȃ��B";
		mes "��������яo���Ă����肵�Ȃ����ȁB";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "9�ځc�c";
		mes "�悵�A�����Ղ�" +getitemname('@item)+ "������������";
		mes "�܂��肪�o�Ă��Ȃ���B";
		next;
		mes "^4A4AFF�����ɂ͉�͂ł܂����B";
		mes "����������đ҂��Ă����";
		mes "���̂����肪�łĂ��܂��B^000000";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�����A�����S�R���ɗ����Ȃ��c�c";
		delitem '@item,10;
		set QUEST_WHITEDAY_NYMPH,2;
		close;
	case 2:
		if(rand(5)) {
			mes "^4A4AFF�܂������ω����Ȃ��悤�ł��ˁB";
			mes "����������ƌ�����Ă��܂��傤�B^000000";
			close;
		}
		mes "["+strcharinfo(0)+"]";
		mes "�肪�o�Ă��Ȃ�!?";
		mes "���܂��ꂽ�̂�������Ȃ��ȁc�c";
		mes "�c�c";
		mes "���I";
		next;
		cutin "white_seed",2;
		mes "-�΂݂ɉ��������ȉ肪�L�тĂ���-";
		mes "-���w���炢�̑傫����";
		mes "�܂邢�t���ς�����-";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "���������āA���ꂪ��t�Ȃ̂��ȁH";
		next;
		mes "^4A4AFF����Ɖ肪�łĂ��܂����ˁB";
		mes "����ł͂������";
		mes "�h�{�������܂��傤�B^000000";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�h�{�Ƃ����ƁH";
		next;
		mes "^4A4AFF�����ł��ˁc�c";
		mes "�~���N�A�����A�����̐��̍��A";
		mes "�����S�W���[�X�A�t�F�C�������n�t�B";
		mes "���̒�����I�ԂƗǂ��ł���B^000000";
		set QUEST_WHITEDAY_NYMPH,3;
		break;
	case 3:
		cutin "white_seed",2;
		mes "^4A4AFF�p�ӂ������̂������Ă��������B^000000";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�c�c�����ƁB";
		next;
		switch(select("�~���N","����","�����̐��̍�","�����S�W���[�X","�t�F�C�������n�t","�����Ă��Ȃ�")) {
			case 1:
				if(countitem(519)<1) {
					mes "^4A4AFF�~���N�������ĂȂ�����Ȃ��ł����I^000000";
					close2;
					cutin "white_seed",255;
					end;
				}
				mes "^4A4AFF�����~���N���T���Ă��������B^000000";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "����c�c";
				mes "�c�c�c�c�c�c�c�c";
				mes "�悵�A�T������B�ǂ����ȁH";
				next;
				mes "^4A4AFF�ł́A���΂炭�҂��܂��傤���B^000000";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�c�c�c�c�c�c�c�c";
				mes "�܂��A�҂̂ł����c�c";
				set QUEST_WHITEDAY_SEX,0;
				delitem 519,1;
				break;
			case 2:
				if(countitem(523)<1) {
					mes "^4A4AFF�����������ĂȂ�����Ȃ��ł����I^000000";
					close2;
					cutin "white_seed",255;
					end;
				}
				mes "^4A4AFF�����������T���Ă��������B^000000";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "����c�c";
				mes "�c�c�c�c�c�c�c�c";
				mes "�悵�A�T������B�ǂ����ȁH";
				next;
				mes "^4A4AFF�ł́A���΂炭�҂��܂��傤���B^000000";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�c�c�c�c�c�c�c�c";
				mes "�܂��A�҂̂ł����c�c";
				set QUEST_WHITEDAY_SEX,0;
				delitem 523,1;
				break;
			case 3:
				if(countitem(1061)<1) {
					mes "^4A4AFF�����̐��̍���";
					mes "�����ĂȂ��݂����ł��ˁB^000000";
					close2;
					cutin "white_seed",255;
					end;
				}
				mes "^4A4AFF���A�����̐��̍��H";
				mes "�{���ɎT���̂ł����H^000000";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "���H�@�����Ė����̐��̍���";
				mes "���ɂ������Ă�����ˁH";
				mes "���炳�炳��`���ƁB";
				mes "�T�����I�@�����A�ǂ�����!?";
				next;
				mes "^4A4AFF�ǂ����Č����Ă��c�c^000000";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�i���������A�N�������Ă�������";
				mes "�������̂Ɂc�c�j";
				mes "����������Ȃ��ˁc�c";
				set QUEST_WHITEDAY_SEX,0;
				delitem 1061,1;
				break;
			case 4:
				if(countitem(531)<1) {
					mes "^4A4AFF�����S�W���[�X��";
					mes "�����ĂȂ��݂����ł��ˁB^000000";
					close2;
					cutin "white_seed",255;
					end;
				}
				mes "^4A4AFF���������S�W���[�X���T���Ă��������B^000000";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�킩�����I";
				mes "�c�c�c�c�c�c�c�c";
				mes "�悵�A�T������B�ǂ����ȁH";
				next;
				mes "^4A4AFF�ł́A���΂炭�҂��܂��傤���B^000000";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�c�c�c�c�c�c�c�c";
				mes "�܂��A�҂̂��c�c";
				set QUEST_WHITEDAY_SEX,1;
				delitem 531,1;
				break;
			case 5:
				if(countitem(1089)<1) {
					mes "^4A4AFF����H�@�t�F�C�������n�t��";
					mes "�����Ă��Ȃ��݂����ł��ˁB^000000";
					close2;
					cutin "white_seed",255;
					end;
				}
				mes "^4A4AFF���H�@�t�F�C�������n�t��";
				mes "�{���ɎT���̂ł����H^000000";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "������񂾂�B";
				mes "�����Ɛ�����ɂȂ��B";
				mes "�΂���[���Ɓc�c";
				mes "�T�����I�@�����A�ǂ���!?";
				next;
				mes "^4A4AFF�c�c�c�c�c�c�c�c";
				mes "�ǂ��Ȃ�̂��҂��Č��܂����c�c^000000";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "���A���̗₽���ԓx�́c�c";
				set QUEST_WHITEDAY_SEX,1;
				delitem 1089,1;
				break;
			case 6:
				mes "^4A4AFF�c�c��������߂Ĉ�ĂĂ��������ˁB";
				mes "�����Ɖh�{�ɂȂ���̂�";
				mes "�����Ă��Ă��������B^000000";
				close2;
				cutin "white_seed",255;
				end;
		}
		set QUEST_WHITEDAY_NYMPH,4;
		break;
	case 4:
		if(rand(5)) {
			cutin "white_seed",2;
			mes "^4A4AFF�܂��A�ω���������܂���ˁB";
			mes "���ɉ����T���Ă݂܂����H^000000";
			next;
			if(countitem(504)) {
				delitem 504,1;
				mes "["+strcharinfo(0)+"]";
				mes "���|�[�V�����c�c����ł������ȁH";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�c�c�����������ˁc�c";
				mes "���\���������񂾂��ǂȂ��B";
			}
			else if(countitem(505)) {
				delitem 505,1;
				mes "["+strcharinfo(0)+"]";
				mes "�|�[�V�����A�ǂ����ȁB";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�c�c�������B";
				mes "����������΂����񂾂낤�c�c";
			}
			else if(countitem(519)) {
				delitem 519,1;
				mes "["+strcharinfo(0)+"]";
				mes "�~���N�A����Ȃ�ǂ����I";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�c�c���߂�";
				mes "�h�{������Ȃ��̂��Ȃ��B";
			}
			else if(countitem(523)) {
				delitem 523,1;
				mes "["+strcharinfo(0)+"]";
				mes "�����I�@����Ȃ炢����I";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "����c�c�������Ȃ���";
				mes "�h�{������Ȃ��̂��Ȃ��B";
			}
			else if(countitem(531)) {
				delitem 531,1;
				mes "["+strcharinfo(0)+"]";
				mes "�����S�W���[�X�A����łǂ����ȁB";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�c�c�������B";
				mes "���\���������񂾂��ǂȂ��B";
			}
			else if(countitem(1061)) {
				delitem 1061,1;
				mes "["+strcharinfo(0)+"]";
				mes "�����̐��̍��A����͂ǂ����ȁB";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�c�c�������B";
				mes "���������Ȃ��B";
			}
			else {
				mes "^4A4AFF�����h�{�̂�����̂��A";
				mes "�����ė��Ă��������B";
				mes "�h�{������Ă��Ȃ��̂��Ǝv���܂��B^000000";
			}
			close2;
			cutin "white_seed",255;
			end;
		}
		mes "^4A4AFF�������낻��ł���B";
		mes "�悭���ĂĂ��������B^000000";
		next;
		mes "^4A4AFF���߂łƂ��������܂��I";
		mes "�j���t���Ăяo�����Ƃ��ł��܂����ˁB^000000";
		next;
		if(QUEST_WHITEDAY_SEX==0)
			cutin "white_choco",2;
		else
			cutin "white_retto",2;
		mes "["+strcharinfo(0)+"]";
		mes "����c�c�{���ɏo���c�c";
		next;
		mes "[�`���R���[�g�j���t]";
		if(QUEST_WHITEDAY_SEX==0)
			mes "�����ɂႠ�c�c";
		else
			mes "���[��c�c";
		set QUEST_WHITEDAY_NYMPH,5;
		break;
	case 5:
		mes "["+strcharinfo(0)+"]";
		mes "���肢�A������x�p�������āc�c";
		next;
		mes "-�΂݂̒��������킯���";
		mes "�����A�����킩�琶�܂ꂽ";
		mes "���̕s���̐��������p�����킵��-";
		next;
		mes "[�`���R���[�g�j���t]";
		if(QUEST_WHITEDAY_SEX==0) {
			mes "�����������Ȃ��`";
			cutin "white_choco",2;
		}
		else {
			mes "�Ă񂾁H";
			cutin "white_retto",2;
		}
		next;
		if(select("�肢��������","���t���ʂ��邩����")==1) {
			mes "["+strcharinfo(0)+"]";
			mes "���`��A���肢�ł����Ă݂悤���ȁB";
			mes "�ł��A�������肢���悤�c�c";
			mes "�Â����������������Ƃ��H";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "�悵�A���̊肢�𕷂��Ă�������!!";
			next;
			if(QUEST_WHITEDAY_SEX==0) {
				mes "[�`�H�`�H�R]";
				mes "�`�H�`�H�R�c�c";
				mes "�����������患�B";
				mes (Sex)? "�p�p�[": "�}�}�[";
			}
			else {
				mes "[���g]";
				mes "�͂��H�@�Ȃ�̂��ƁH";
				mes "����Ȃ��ƌ����Ă�ɂ���������";
				mes "�H�ׂ���̂����Ă��Ă�B";
			}
		}
		else {
			mes "["+strcharinfo(0)+"]";
			if(QUEST_WHITEDAY_SEX==0) {
				mes "�����ƁA�Ȃɂ��b���邩�ȁH";
				mes "�����Ă�Ӗ��킩��H";
				next;
				mes "[�`�H�`�H�R]";
				mes "�`�H�`�H�R�c�c";
				mes "�����������患�B";
				mes (Sex)? "�p�p�[": "�}�}�[";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "�����Ƃ���ׂ��񂾂ˁB";
			}
			else {
				mes "���A�����̓�̐����́B";
				mes "���t�͂킩�邩���H";
				next;
				mes "[���g]";
				mes "��̐����̂ł͂Ȃ����g���B";
				next;
				mes "["+strcharinfo(0)+"]";
				mes "���A�����Ƙb����I";
			}
		}
		next;
		mes "^4A4AFF�ǂ����";
		mes "�����������Ă���݂����ł��ˁB^000000";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "������I�@�т����肵��!!";
		mes "�ˑR�o�Ă��Ȃ��ł�I";
		mes "�Ȃ������l���A";
		mes "�������邱�Ƃ΂��肷��񂾁H";
		next;
		mes "^4A4AFF���߂�Ȃ����B";
		mes "�����������Ȃ�������ł���c�c^000000";
		next;
		mes "^4A4AFF�����Ƌ�����������Ȃ����ǁA";
		mes "���͐l�H�m�\�}�j���A���V�X�e����";
		mes "���������Ȃ�ł���B";
		mes "���U��x��";
		mes "�`���R���[�g�����炦�Ȃ�����";
		mes "�₵���₵�����Ȃ�ł��c�c^000000";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�c�c�i���̃A���P�~�X�g�͈�́c�c�j";
		mes "�N�̎���Ȃ�Ăǂ��ł�������I";
		next;
		if(QUEST_WHITEDAY_SEX==0) {
			mes "[�`�H�`�H�R]";
			mes "�c�c�`���R���[�g�ق����H";
		}
		else {
			mes "[���g]";
			mes "�`���R���[�g�~�����̂����H";
		}
		next;
		mes "["+strcharinfo(0)+"]";
		mes "���H�@���Ȃ�Č������́H";
		next;
		if(QUEST_WHITEDAY_SEX==0) {
			mes "[�`�H�`�H�R]";
			mes "�c�c";
			mes "�j���W���B";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "��H";
			mes "�j���W�����ǂ��������́H";
		}
		else {
			mes "[���g]";
			mes "�c�c";
			mes "�������������B";
			mes "�j���W�������Ă��Ă�B";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "�c�c�����H";
			mes "�������A�ʂ̂��ƌ����Ă���ˁc�c";
		}
		next;
		mes "^4A4AFF�j���W�����~�����݂����ł��ˁB";
		mes "�����Ɩʓ|���݂Ȃ��ƃ_���ł���B^000000";
		next;
		mes "["+strcharinfo(0)+"]";
		mes "�����A�������A�Ȃ�قǁc�c";
		mes "���v�A�����Ɩʓ|�݂��B";
		mes "�j���W���Ȃ�ė~�����̂��c�c";
		set QUEST_WHITEDAY_NYMPH,6;
		break;
	default:	//6�`9�̂Ƃ�
		switch(QUEST_WHITEDAY_NYMPH) {
			case 6: set '@item,515; break;	//�j���W��
			case 7: set '@item,512; break;	//�����S
			case 8: set '@item,519; break;	//�~���N
			case 9: set '@item,1061; break;	//�����̐��̍�
		}
		if(countitem('@item)<1) {
			mes "^4A4AFF" +getitemname('@item)+ "���܂�";
			mes "�p�ӂ��Ă��Ȃ�������ł����H^000000";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "�N�Ɍ����Ȃ��Ă�";
			mes "�킩���Ă��!!";
			close;
		}
		cutin "white_seed",2;
		mes "["+strcharinfo(0)+"]";
		mes "���[���A" +getitemname('@item)+ "�����Ă�����B";
		mes "�ǂ��ɂ���́H";
		next;
		if(QUEST_WHITEDAY_SEX==0) {
			cutin "white_choco",2;
			mes "[�`�H�`�H�R]";
			mes "�`�H�`�H�R�����ɂ���[�B";
			mes getitemname('@item)+ "��������[�B";
			set QUEST_WHITEDAY_NYMPH,rand(6,10);
			switch(QUEST_WHITEDAY_NYMPH) {
			case 6:
				mes "�`�H�`�H�R�A�j���W���D���B";
				break;
			case 7:
				mes "�`�H�`�H�R�A�����S�D���B";
				break;
			case 8:
				mes "�`�H�`�H�R�A�~���N�D���B";
				break;
			case 9:
				mes "�`�H�`�H�R�A";
				mes "�L���L�������̐��̍��~�����B";
				break;
			case 10:
				mes "�`�H�`�H�R�A";
				mes "�L���L�������ȕ�Η~�����B";
				break;
			}
		}
		else {
			cutin "white_retto",2;
			mes "[���g]";
			mes "�������������B";
			mes "�{���Ɏ����ė����ȁI";
			mes "���肪�Ƃ��I";
			set QUEST_WHITEDAY_NYMPH,rand(6,10);
			switch(QUEST_WHITEDAY_NYMPH) {
			case 6:
				mes "�Ƃ���Ńj���W���Ȃ��H";
				mes "�ڂ��o�߂��΂����";
				mes "�h�{����������K�v�Ȃ񂾁B";
				break;
			case 7:
				mes "�Ƃ���Ń����S�Ȃ��H";
				mes "�ڂ��o�߂��΂����";
				mes "�h�{����������K�v�Ȃ񂾁B";
				break;
			case 8:
				mes "�Ƃ���Ń~���N�Ȃ��H";
				mes "�ڂ��o�߂��΂����";
				mes "�h�{����������K�v�Ȃ񂾁B";
				break;
			case 9:
				mes "�Ƃ���Ŗ����̐��̍��Ȃ��H";
				mes "�ڂ��o�߂��΂����";
				mes "���͂���������K�v�Ȃ񂾁B";
				break;
			case 10:
				mes "�_�C�������h�����Ă�H";
				mes "�������炿�傤�����B";
				mes "�����Ƃ��Ԃ��͂����B";
				break;
			}
		}
		delitem '@item,1;
		break;
	case 10:
		if(countitem(730)<1) {
			mes "^4A4AFF���́[�c�c";
			mes "�_�C�������h1�J���b�g����Ȃ���";
			mes "���߂݂����ł���B^000000";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "1�J���b�g���c�c";
			close;
		}
		if(QUEST_WHITEDAY_SEX==0) {
			cutin "white_choco",2;
			mes "["+strcharinfo(0)+"]";
			mes "���[���A�`���R����Ȃ��ă`�H�`�H�R�I";
			mes "�_�C�������h1�J���b�g�����Ă�����B";
			next;
			mes "[�`�H�`�H�R]";
			mes "����[���I";
			mes "�{���ɂ��肪�Ƃ��B";
			mes "�����Ȃɂ����Ԃ����Ȃ�����ˁB";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "�i����H�@�}�Ɍ��t�����܂��Ȃ��Ă�j";
			mes "�������Ă����̂��ȁH";
			next;
			mes "[�`�H�`�H�R]";
			mes "�`�H�`�H�R�̓`���R���[�g�̃`�H�R�I";
			mes "���������`���R���[�g�������[�B";
		}
		else {
			cutin "white_retto",2;
			mes "["+strcharinfo(0)+"]";
			mes "���[���A���g�A����H";
			mes "�_�C�������h1�J���b�g��";
			mes "�����Ă�����B";
			next;
			mes "[���g]";
			mes "��[�A���悢�悾�B";
			mes "����ŋA�邱�Ƃ��ł���B";
			mes "�A���͂����B";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "�i�A�邾���āH�j";
			mes "���������Ă�́H";
			next;
			mes "[�`�H�`�H�R]";
			mes "���g�̓`���R���[�g�j���t�B";
			mes "���͂��̎��������A";
			mes "�l���Ăяo�������R��";
			mes "�Ȃ�ƂȂ��킩���B";
			mes "�`���R���[�g���~�����̂��ȁH";
		}
		delitem 730,1;
		set QUEST_WHITEDAY_NYMPH,11;
		break;
	case 11:
		if(QUEST_WHITEDAY_SEX==0) {
			cutin "white_choco",2;
			mes "[�`�H�`�H�R]";
		}
		else {
			cutin "white_retto",2;
			mes "[���g]";
		}
		mes "����Ɏז��҂͂��Ȃ��H";
		next;
		switch(select("�l������","�����X�^�[������","�N�����Ȃ�")) {
			case 1:
				if(QUEST_WHITEDAY_SEX==0) {
					mes "[�`�H�`�H�R]";
					mes "�l������Α������N����B";
					mes "�`�H�`�H�R�͌������Ȃ���B";
					mes "�������ƂȂ�Č������Ȃ���B";
				}
				else {
					mes "[���g]";
					mes "�l�͓����Ȃ̂ɂ������܂�����c�c";
					mes "����Ȃ̂͌������Ȃ��c�c";
				}
				break;
			case 2:
				if(QUEST_WHITEDAY_SEX==0) {
					mes "[�`�H�`�H�R]";
					mes "�����X�^�[����́c�c�H";
					mes "�����X�^�[�|���患�B";
				}
				else {
					mes "[���g]";
					mes "�����X�^�[�ɏP��ꂽ���͂Ȃ��B";
					mes "�܂���ɂ��悤�c�c";
				}
				break;
			case 3:
				if(QUEST_WHITEDAY_SEX==0) {
					mes "[�`�H�`�H�R]";
					mes "^00FF00�{���ɁH^000000";
				}
				else {
					mes "[���g]";
					mes "^00FF00�{���ɒN�����Ȃ��H^000000";
				}
				next;
				if(select("�{���͂���","�{���ɂ��Ȃ�")==1) {
					if(QUEST_WHITEDAY_SEX==0) {
						mes "[�`�H�`�H�R]";
						mes "���߂���B";
						mes "�`�H�`�H�R�݂͂�Ȃ��~�ɖڂ�������";
						mes "�����̂��������Ȃ���B";
					}
					else {
						mes "[���g]";
						mes "�l�������Ă���p�Ȃ��";
						mes "�������Ȃ��I";
					}
					break;
				}
				if(QUEST_WHITEDAY_SEX==0) {
					mes "[�`�H�`�H�R]";
					mes "�`�H�`�H�R����ĂĂ����";
					mes "���肪�Ƃ���";
					if(rand(2)) {
						getitem 560,6;
						getitem 559,2;
						getitem 558,2;
					}
					else {
						getitem 559,1;
						getitem 560,3;
						getitem 530,3;
					}
				}
				else {
					mes "[���g]";
					mes "�킩�����I";
					mes "�l����ĂĂ��ꂽ�����n����I";
					if(rand(2)) {
						getitem 560,4;
						getitem 573,3;
					}
					else {
						getitem 560,4;
						getitem 559,2;
						getitem 558,2;
					}
				}
				set QUEST_WHITEDAY_NYMPH,12;
				break;
		}
		break;
	case 12:
		if(QUEST_WHITEDAY_SEX==0) {
			cutin "white_choco",2;
			mes "[�`�H�`�H�R]";
			mes "���ꂮ�炢�ł����H";
			mes "�`���R���[�g��R����B";
			next;
			menu "���̃`���R���[�g�������痈���́H",-;
			mes "[�`�H�`�H�R]";
			mes "�`�H�`�H�R�̓`���R���[�g�̗d���B";
			mes "�����ǁA�����Ȃ��Ƀ`���R���[�g��";
			mes "���̂͂ł��Ȃ��́B";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "��!?";
			mes "���Ⴀ�ǂ�����č���Ă�́H";
			next;
			mes "[�`�H�`�H�R]";
			mes "���Ƃ������A";
			mes "�`���R���[�g���o�Ă���́B";
			mes "�������痈���̂�������Ȃ����ǁB";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "�`���R���[�g���o�Ă���H";
			mes "���ꂽ�`���R���[�g��";
			mes "�ˑR�����������͂��ꂾ�����񂾁I";
			next;
			mes "[�`�H�`�H�R]";
			mes "���H�@�`�H�`�H�R�͂����s����B";
			mes "���Ⴀ�ˁI";
		}
		else {
			cutin "white_retto",2;
			mes "[���g]";
			mes "�ǂ����ȁH";
			mes "���̈ʂŏ\���H";
			next;
			menu "�`���R���[�g���ǂ�����č�����́H",-;
			mes "[���g]";
			mes "���g�̓`���R���[�g�j���t�����ǁA";
			mes "�ޗ����Ȃ��Ƀ`���R���[�g��";
			mes "��邱�Ƃ͂ł��Ȃ���B";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "��!?";
			mes "���Ⴀ�ǂ�����č���Ă�́H";
			next;
			mes "[���g]";
			mes "���Ƃ������A";
			mes "�`���R���[�g���o�ė���񂾂�ȁB";
			mes "��������o�Ă��Ă���̂��́A";
			mes "������Ȃ��񂾂��ǂˁB";
			next;
			mes "["+strcharinfo(0)+"]";
			mes "�`���R���[�g���o�Ă���H";
			mes "���ꂽ�`���R���[�g��";
			mes "�ˑR�����������͂��ꂾ�����񂾁I";
			next;
			mes "[���g]";
			mes "���ꂶ�Ⴀ�A�l�͋A���B";
			mes "���Ⴀ�ˁB";
		}
		if(QUEST_WHITEDAY==5)
			set QUEST_WHITEDAY,6;
		set QUEST_WHITEDAY_NYMPH,13;
		set QUEST_WHITEDAY_SEX,0;
		set QUEST_WHITEDAY_POSITION,0;
		break;
	}
	close2;
	cutin "dummy",255;
	end;
}

pay_fild01.gat,216,118,0	duplicate(�΂�#NYMPH)	�΂�#1	111
pay_fild01.gat,176,120,0	duplicate(�΂�#NYMPH)	�΂�#2	111
pay_fild01.gat,192,212,0	duplicate(�΂�#NYMPH)	�΂�#3	111
pay_fild03.gat,158,80,0		duplicate(�΂�#NYMPH)	�΂�#4	111
pay_fild03.gat,351,106,0	duplicate(�΂�#NYMPH)	�΂�#5	111
pay_fild03.gat,194,212,0	duplicate(�΂�#NYMPH)	�΂�#6	111
pay_fild08.gat,222,183,0	duplicate(�΂�#NYMPH)	�΂�#7	111
pay_fild08.gat,237,349,0	duplicate(�΂�#NYMPH)	�΂�#8	111
pay_fild08.gat,61,345,0		duplicate(�΂�#NYMPH)	�΂�#9	111


/* ���z���C�g�f�[
alberta.gat,188,64,4	script	���َq���l	91,{
	mes "[���C�v��=�V���K�[]";
	mes "��������Ⴂ�܂��I";
	mes "�����������َq�͂������ł����H";
	mes "���t���l�c�c����Ȃ��āc�c";
	mes "�A���x���^�̂��َq�E�l��";
	mes "�ꐶ�������������َq�ł��B";
	next;
	mes "[���C�v��=�V���K�[]";
	mes "�������l����������";
	mes "�v���[���g�̂��Ԃ��Ɂc�c";
	mes "�S�̂��������A���܂��`��";
	mes "���َq�͂������ł����`�H";
	next;
	switch (select("���������I","����܂���B","���t���l�H")) {
	case 1:
		mes "[���C�v��=�V���K�[]";
		mes "�͂��I";
		mes "�����炪���j���[�ɂȂ��Ă���܂��`";
		mes "���Ɍ��肪���邽��";
		mes "�܂Ƃߔ�����^ff00005��^000000�܂ł�";
		mes "�����Ă��������܂��ˁB";
		next;
		switch (select("�L�����f�B","�X�e�B�b�N�L�����f�B","�悭�Ă����N�b�L�[")) {
			case 1:
				set '@itemid,529;
				set '@price,3000;
				break;
			case 2:
				set '@itemid,530;
				set '@price,4000;
				break;
			case 3:
				set '@itemid,538;
				set '@price,2000;
				break;
		}
		mes "[���C�v��=�V���K�[]";
		mes getitemname('@itemid)+ "�ł��ˁ`";
		mes '@price+ "Zeny�ɂȂ�܂��`";
		mes "�����������グ�ł����H";
		while(1) {
			next;
			input '@num;
			if('@num>5) {
				mes "[���C�v��=�V���K�[]";
				mes "����H�����H";
				mes "�����Ă������Ⴂ�܂����H";
				mes "���A������������������݂����ł��B";
				mes "������x����������Ă��������܂����H";
				mes "��x�ɂ������グ����������̂�";
				mes "^ff00005��^000000�܂łł��`";
				continue;
			}
			break;
		}
		if('@num<=0)
			break;	//switch��������
		if(Zeny<'@price*'@num) {
			mes "[���C�v��=�V���K�[]";
			mes "�����c�c�H";
			mes "���q�l�A����������܂����`";
			mes "������x�����������m���߂̏�";
			mes "�����������肢�������܂��`";
			close;
		}
		set Zeny,Zeny-'@price*'@num;
		getitem '@itemid,'@num;
		mes "[���C�v��=�V���K�[]";
		mes "���肪�Ƃ��������܂��`";
		mes "���̂��َq�A�{���ɔ���������ł���`";
		mes "���t���̂��َq�͐��E��Ȃ�ł�����I";
		mes "�������ŐH�ׂĂ���낵���ł�����";
		mes "��݂��ɂȂ��đ���Ȃ��悤��";
		mes "�C�����Ă��������˂��B";
		close;
	case 2:
		break;
	case 3:
		mes "[���C�v��=�V���K�[]";
		mes "���A�͂��B";
		mes "���̂��t���l��";
		mes "���َq�E�l�̃N�[�x���l��";
		mes "�L���V���l�ł��B";
		mes "����l�̌��ł��َq����";
		mes "�C�s�������Ă��������Ă���܂��`";
		next;
		mes "[���C�v��=�V���K�[]";
		mes "���̎����͂��t���l�B�����X��";
		mes "�ړ��̔������Ă���炵���̂ł����c�c";
		mes "���ǂ��ɋ�����̂��́A";
		mes "���ɂ͂킩��܂��񂯂ǁA";
		mes "�ǂ����ŉ�邩������܂���ˁ`";
		close;
	}
	mes "[���C�v��=�V���K�[]";
	mes "�����ł����c�c�c�O�ł��B";
	mes "���΂炭�͂������";
	mes "�̔������Ă��������܂��̂ŁA";
	mes "�����~�����Ȃ�����A";
	mes "�������������������I";
	if(@menu==2)
		close;
	next;
	mes "[���C�v��=�V���K�[]";
	mes "�������A���Ɍ��肪�������܂��̂�";
	mes "�����߂Ɂc�c";
	mes "���肪�Ƃ��������܂����B";
	close;
}
*/